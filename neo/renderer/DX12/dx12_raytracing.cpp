#pragma hdrstop

#include <stdexcept>
#include <algorithm>

#include "../tr_local.h"

#include "./dx12_raytracing.h"
#include "./dx12_DeviceManager.h"
#include "./dx12_RenderPass.h"

idCVar s_raysCastPerLight("s_raysCastPerLight", "20", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "number of shadow rays per light per pixel.", 0, 1000);
idCVar s_lightEmissiveRadius("s_lightEmissiveRadius", "20.0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "the radius of a light. The larger the value, the softer shadows will be.", 0.0f, 100.0f);
idCVar r_useGli("r_useGli", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use raytraced global illumination.");
idCVar r_gliResolution("r_gliResolution", "0.33" /*"0.125"*/, CVAR_RENDERER | CVAR_FLOAT, "the percentage of global illumination points to cast per screen axis.", 0.0f, 1.0f);
idCVar r_gliBounces("r_gliBounces", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "the max number of global illumination bounces allowed.", 0, 10);

namespace DX12Rendering {
	bool CheckRaytracingSupport() {
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};

		if (!DX12Rendering::WarnIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
			return false;
		}

		if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
			DX12Rendering::WarnMessage("Raytracing Tier is not supported.");
			return false;
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
		if (!DX12Rendering::WarnIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
			return false;
		}

		if (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
			DX12Rendering::WarnMessage("Binding Tier is not supported.");
			return false;
		}

		return true;
	}

	Raytracing::Raytracing(UINT screenWidth, UINT screenHeight) :
		isRaytracingSupported(CheckRaytracingSupport()),
		m_rayGenSignature(WRITE_OUTPUT | READ_ENVIRONMENT),
		m_missSignature(NONE),
		m_hitSignature(WRITE_OUTPUT | READ_ENVIRONMENT),
		m_width(screenWidth),
		m_height(screenHeight),
		m_tlasManager(&m_blasManager),
		m_nextDescriptorHeapIndex(0),
		m_constantBuffer({})
		//m_localVertexBuffer(VERTCACHE_VERTEX_MEMORY),
		//m_localIndexBuffer(VERTCACHE_INDEX_MEMORY)
	{
		CreateShadowPipeline();
		CreateGlobalIlluminationPipeline();
		CreateCBVHeap(sizeof(m_constantBuffer));
	}

	Raytracing::~Raytracing()
	{
	}

	void Raytracing::BeginFrame()
	{
		// Update any BLAS Content
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		if (device == nullptr)
		{
			return;
		}

		m_nextDescriptorHeapIndex = 0;

		{
			// Update per frame constants;
			DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();
			assert(textureManager);

			//Update the resources
			m_constantBuffer.positionTextureIndex = textureManager->GetGlobalTexture(eGlobalTexture::POSITION)->GetTextureIndex();
			m_constantBuffer.flatNormalIndex = textureManager->GetGlobalTexture(eGlobalTexture::WORLD_FLAT_NORMALS)->GetTextureIndex();
			m_constantBuffer.normalIndex = textureManager->GetGlobalTexture(eGlobalTexture::WORLD_NORMALS)->GetTextureIndex();
			m_constantBuffer.diffuseTextureIndex = textureManager->GetGlobalTexture(eGlobalTexture::ALBEDO)->GetTextureIndex();
			m_constantBuffer.specularTextureIndex = textureManager->GetGlobalTexture(eGlobalTexture::SPECULAR_COLOR)->GetTextureIndex();

			m_constantBuffer.raysPerLight = s_raysCastPerLight.GetInteger();

			// Update the noise offset
			m_constantBuffer.noiseOffset += 0.01f;
			if (m_constantBuffer.noiseOffset > 5.0f)
			{
				m_constantBuffer.noiseOffset = 0.0f;
			}
		}

		CleanUpAccelerationStructure();
	}

	void Raytracing::EndFrame()
	{
		m_nextObjectIndex = 0;
	}

	void Raytracing::CreateCBVHeap(const size_t constantBufferSize)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		m_cbvHeapIncrementor = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	void Raytracing::UpdateGeometryDescriptors(UINT frameIndex, UINT objectIndex)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle = GetDescriptorHandle(frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::SRV_GEOMETRY);
			
			GenericWriteBuffer* resourceBuffer = m_blasManager.GetGeometryBuffer();
			device->CreateShaderResourceView(resourceBuffer->resource.Get(), resourceBuffer->GetSrvDescriptorView(), handle);
		}

		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle = GetDescriptorHandle(frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::SRV_VERTEX);

			DX12Rendering::Geometry::VertexBuffer*  vertexBuffer = static_cast<DX12Rendering::Geometry::VertexBuffer*>(vertexCache.staticData.vertexBuffer.GetAPIObject());

			device->CreateShaderResourceView(vertexBuffer->resource.Get(), vertexBuffer->GetSrvDescriptorView(), handle);
		}

		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle = GetDescriptorHandle(frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::SRV_INDEX);

			DX12Rendering::Geometry::IndexBuffer* indexBuffer = static_cast<DX12Rendering::Geometry::IndexBuffer*>(vertexCache.staticData.indexBuffer.GetAPIObject());

			device->CreateShaderResourceView(indexBuffer->resource.Get(), indexBuffer->GetSrvDescriptorView(), handle);
		}
	}

	void Raytracing::UpdateTlasDescriptor(const UINT frameIndex, const UINT objectIndex)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Write the acceleration structure to the view.
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = GetDescriptorHandle(frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::SRV_TLAS);
		
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_tlasManager.GetCurrent().resource->GetGPUVirtualAddress(); // Write the acceleration structure view in the heap 

		device->CreateShaderResourceView(nullptr, &srvDesc, shadowHandle);
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC Raytracing::SetCBVDescriptorTable(const size_t constantBufferSize, const void* constantBuffer, const UINT frameIndex, const UINT objectIndex, const DX12Rendering::e_RaytracingHeapIndex heapIndex) {
		DX12Rendering::ResourceManager& resourceManager = *DX12Rendering::GetResourceManager();

		// Copy the CBV value to the upload heap
		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(constantBufferSize);
		resourceManager.FillConstantBuffer(buffer, constantBuffer);

		// Create the constant buffer view for the object
		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetDescriptorHandle(frameIndex, objectIndex, heapIndex);

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		device->CreateConstantBufferView(&buffer.bufferLocation, descriptorHandle);

		return buffer.bufferLocation;
	}

	void Raytracing::CreateOutputBuffers()
	{
		//TODO: Make shadow atlas for lights. Right now were putting the depth into the diffuse buffer for testing
	}

	void Raytracing::Resize(UINT width, UINT height)
	{
		m_width = width;
		m_height = height;

		DX12Rendering::GetSurface(eRenderSurface::RaytraceShadowMask)->Resize(width, height);

		// TODO: Check if the buffers already exists and clear them
		CreateOutputBuffers();
		CreateShaderResourceHeap();
		CreateShaderBindingTables();
	}

	void Raytracing::SetOutputTexture(DX12Rendering::eRenderSurface renderSurface, UINT frameIndex, UINT objectIndex, DX12Rendering::e_RaytracingHeapIndex uav)
	{
		RenderSurface* duffuseSurface = DX12Rendering::GetSurface(renderSurface);
		D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = GetDescriptorHandle(frameIndex, objectIndex, uav);

		duffuseSurface->CreateUnorderedAccessView(uavHandle);
	}

	void Raytracing::CreateShaderResourceHeap()
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create a SRV/UAV/CBV descriptor heap.
		// 3 UAV for the raytracing output
		// 1 SRV for the TLAS
		// 1 CBV for the Camera properties
		// 1 CBV for the Light properties
		m_generalUavHeaps = CreateDescriptorHeap(device, DESCRIPTOR_HEAP_TOTAL, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Raytracing::GetDescriptorHandle(
		const UINT frameIndex,
		const UINT objectIndex,
		const DX12Rendering::e_RaytracingHeapIndex heapIndex)
	{
		// TODO: Speed this up.
		assert(objectIndex < DESCRIPTOR_OBJECT_TOTAL);
		assert(frameIndex < DX12_FRAME_COUNT);

		const UINT index = (frameIndex * DESCRIPTOR_HEAP_TOTAL) + (objectIndex * DESCRIPTOR_HEAP_SIZE) + heapIndex;
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart(), index, m_cbvHeapIncrementor);

		return descriptorHandle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE Raytracing::GetGPUDescriptorHandle(
		const UINT frameIndex,
		const UINT objectIndex)
	{
		// TODO: Speed this up.
		assert(objectIndex < DESCRIPTOR_OBJECT_TOTAL);
		assert(frameIndex < DX12_FRAME_COUNT);

		const UINT index = (frameIndex * DESCRIPTOR_HEAP_TOTAL) + (objectIndex * DESCRIPTOR_HEAP_SIZE);
		CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHandle(m_generalUavHeaps->GetGPUDescriptorHandleForHeapStart(), index, m_cbvHeapIncrementor);

		return descriptorHandle;
	}

	void Raytracing::CleanUpAccelerationStructure()
	{
		// TODO: Add any acceleration structure cleanup.
	}

	void Raytracing::GenerateTLAS()
	{
		// Update all needed child BLAS
		m_tlasManager.UpdateDynamicInstances();

		// Update the resource
		if (!m_tlasManager.Generate())
		{
			return;
		}

		if (m_tlasManager.GetCurrent().state != Resource::eResourceState::Ready)
		{
			return;
		}
	}

	void Raytracing::Uniform4f(UINT index, const float* uniform)
	{
		memcpy(&m_constantBuffer.renderParameters[index], uniform, sizeof(XMFLOAT4));
	}

	void Raytracing::ResetLightList()
	{
		m_constantBuffer.lightCount = 0;
	}

	UINT Raytracing::GetLightMask(const UINT lightIndex)
	{
		for (UINT index = 0; index < m_constantBuffer.lightCount; ++index)
		{
			if (m_lights[index].lightIndex == lightIndex)
			{
				return 0x00000001 << index;
			}
		}

		return 0x00000000;
	}

	bool Raytracing::AddLight(const UINT lightIndex, const DXR_LIGHT_TYPE type, const DX12Rendering::TextureBuffer* falloffTexture, const DX12Rendering::TextureBuffer* projectionTexture, const UINT shadowMask, const XMFLOAT4& location, const XMFLOAT4& color, const XMFLOAT4* lightProjection, const XMFLOAT4& scissorWindow, bool castsShadows)
	{
		static UINT padValue = 0;
		padValue = (padValue + 1) % 3459871;

		UINT index = m_constantBuffer.lightCount;
		if(index >= MAX_SCENE_LIGHTS)
		{ 
			//assert(false, "Raytracing::AddLight: Too many lights.");
			return false;
		}

		m_lights[index].falloffIndex = 0;
		m_lights[index].projectionIndex = 0;

		m_lights[index].flags = 0;

		m_lights[index].flagValue.castsShadows = castsShadows;

		{
			// Calculate type properties
			if ((type & DXR_LIGHT_TYPE_AMBIENT) > 0)
			{
				m_lights[index].flagValue.isAmbientLight = true;
			}
			else
			{
				// All lights that are not ambient are point lights
				m_lights[index].flagValue.isPointLight = true;
			}

			if ((type & DXR_LIGHT_TYPE_FOG) > 0)
			{
				m_lights[index].flagValue.isFogLight = true;
			}
		}

		m_lights[index].lightIndex = lightIndex;
		m_lights[index].shadowMask = shadowMask;

		m_lights[index].emissiveRadius = s_lightEmissiveRadius.GetFloat();

		m_lights[index].center = location;

		m_lights[index].scissor = scissorWindow;

		m_lights[index].color = color;

		m_lights[index].projectionS = lightProjection[0];
		m_lights[index].projectionT = lightProjection[1];
		m_lights[index].projectionQ = lightProjection[2];
		m_lights[index].falloffS = lightProjection[3];

		m_lights[index].falloffIndex = falloffTexture->GetTextureIndex();
		m_lights[index].projectionIndex = projectionTexture->GetTextureIndex();

		++m_constantBuffer.lightCount;

		return true;
	}

	const DX12Rendering::Commands::FenceValue Raytracing::CastGlobalIlluminationRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect
	)
	{
		constexpr UINT surfaceCount = 1;
		const DX12Rendering::eRenderSurface surfaces[surfaceCount] = {
			DX12Rendering::eRenderSurface::GlobalIllumination
		};

		DX12Rendering::RenderPassBlock renderPass(
			"RayTracing::CastGlobalIlluminationRays",
			Commands::COMPUTE,
			surfaces,
			surfaceCount
		);

		auto tlasManager = GetTLASManager();

		if (tlasManager && !tlasManager->IsReady()) {
			// No objects to cast light against.
			return DX12Rendering::Commands::FenceValue(DX12Rendering::Commands::COMPUTE, 0);
		}

		auto commandManager = DX12Rendering::Commands::GetCommandManager(Commands::COMPUTE);
		DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RayTracing::CastGlobalIlluminationRays");

		commandManager->InsertFenceWait(DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->GetLastFenceValue());
		commandManager->InsertFenceWait(m_tlasManager.GetCurrent().GetLastFenceValue());

		const UINT objectIndex = RequestNewObjectIndex();

		LONG xResolution = static_cast<LONG>(std::max(viewport.Width * r_gliResolution.GetFloat(), 2.0f));
		LONG yResolution = static_cast<LONG>(std::max(viewport.Height * r_gliResolution.GetFloat(), 2.0f));

		dxr_global_illumination_t constants(
			viewport.Width / xResolution, viewport.Height / yResolution,
			r_gliBounces.GetInteger(),
			s_raysCastPerLight.GetInteger(), // TODO: Give it's own variable.
			m_lights, m_constantBuffer.lightCount
		);

		SetCBVDescriptorTable(sizeof(dxr_global_illumination_t), &constants, frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::CBV_LightProperties);

		const CD3DX12_RECT castingDimensions(
				0, 0,
				xResolution, yResolution);

		// Add the output buffers
		SetOutputTexture(eRenderSurface::GlobalIllumination, frameIndex, objectIndex, e_RaytracingHeapIndex::UAV_ShadowMap);

		CastRays(frameIndex, objectIndex, viewport, castingDimensions, renderPass, m_gliStateObject.Get(), m_gliStateObjectProps.Get());

		const DX12Rendering::Commands::FenceValue result = commandManager->InsertFenceSignal();

		return result;
	}

	const DX12Rendering::Commands::FenceValue Raytracing::CastShadowRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect
	)
	{
		constexpr UINT surfaceCount = 3;
		const DX12Rendering::eRenderSurface surfaces[surfaceCount] = {
			DX12Rendering::eRenderSurface::RaytraceShadowMask,
			DX12Rendering::eRenderSurface::Diffuse,
			DX12Rendering::eRenderSurface::Specular
		};

		DX12Rendering::RenderPassBlock renderPass(
			"RayTracing::CastShadowRays",
			Commands::COMPUTE,
			surfaces,
			surfaceCount
		);

		auto tlasManager = GetTLASManager();

		if (tlasManager && !tlasManager->IsReady()) {
			// No objects to cast shadows.
			return DX12Rendering::Commands::FenceValue(DX12Rendering::Commands::COMPUTE, 0);
		}

		auto commandManager = DX12Rendering::Commands::GetCommandManager(Commands::COMPUTE);
		DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RayTracing::CastShadowRays");

		commandManager->InsertFenceWait(DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->GetLastFenceValue());
		commandManager->InsertFenceWait(m_tlasManager.GetCurrent().GetLastFenceValue());

		//for (UINT index = 0; index < m_constantBuffer.lightCount; ++index)
		std::for_each(std::begin(m_lights), std::begin(m_lights) + m_constantBuffer.lightCount,
				[&](dxr_lightData_t& light)
		{
			const UINT objectIndex = RequestNewObjectIndex();

			SetCBVDescriptorTable(sizeof(light), &light, frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::CBV_LightProperties);
			//std::memcpy(&m_activeLight, &m_lights[index], sizeof(dxr_lightData_t));

			const CD3DX12_RECT lightViewport(
				light.scissor.x, light.scissor.y,
				light.scissor.z, light.scissor.w);

			// Add the output buffers
			SetOutputTexture(eRenderSurface::RaytraceShadowMask, frameIndex, objectIndex, e_RaytracingHeapIndex::UAV_ShadowMap); // TODO: Remove
			SetOutputTexture(eRenderSurface::Diffuse, frameIndex, objectIndex, e_RaytracingHeapIndex::UAV_DiffuseMap);
			SetOutputTexture(eRenderSurface::Specular, frameIndex, objectIndex, e_RaytracingHeapIndex::UAV_SpecularMap);

			CastRays(frameIndex, objectIndex, viewport, lightViewport, renderPass, m_shadowStateObject.Get(), m_shadowStateObjectProps.Get());
		});

		const DX12Rendering::Commands::FenceValue result = commandManager->InsertFenceSignal();

		return result;
	}

	const void Raytracing::CastRays(
		const UINT frameIndex,
		const UINT objectIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect,
		DX12Rendering::RenderPassBlock& renderPass,
		ID3D12StateObject* pipelineState,
		ID3D12StateObjectProperties* stateProperties
	)
	{
		auto commandList = renderPass.GetCommandManager()->RequestNewCommandList();

		//commandList->AddPreFenceWait(m_tlasManager.GetCurrent().GetLastFenceValue());

		// Copy the CBV data to the heap
		const float scissorVector[4] = { static_cast<float>(scissorRect.left), static_cast<float>(scissorRect.top), static_cast<float>(scissorRect.right), static_cast<float>(scissorRect.bottom) };
		Uniform4f(RENDERPARAM_SCISSOR, scissorVector);

		const float viewportVector[4] = { viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height };
		Uniform4f(RENDERPARM_VIEWPORT, viewportVector);

		SetCBVDescriptorTable(sizeof(m_constantBuffer), &m_constantBuffer, frameIndex, objectIndex, DX12Rendering::e_RaytracingHeapIndex::CBV_CameraProperties);
		
		UpdateTlasDescriptor(frameIndex, objectIndex);
		UpdateGeometryDescriptors(frameIndex, objectIndex);

		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			std::vector<ID3D12DescriptorHeap*> heaps = { GetUavHeap() };
			commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

			// Create the ray dispatcher
			const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = CreateShadowBindingTable(frameIndex, objectIndex, stateProperties)->GetGPUVirtualAddress();
			const UINT32 generatorSize = m_generalSBTDesc.GetGeneratorSectorSize();
			const UINT32 missSize = m_generalSBTDesc.GetMissSectorSize();
			const UINT32 hitSize = m_generalSBTDesc.GetHitGroupSectorSize();
			const UINT32 callableShadersSize = m_generalSBTDesc.GetCallableShaderSectorSize();

			D3D12_DISPATCH_RAYS_DESC desc = {};
			desc.RayGenerationShaderRecord.StartAddress = gpuAddress;
			desc.RayGenerationShaderRecord.SizeInBytes = generatorSize;

			desc.MissShaderTable.StartAddress = gpuAddress + generatorSize;
			desc.MissShaderTable.SizeInBytes = missSize;
			desc.MissShaderTable.StrideInBytes = m_generalSBTDesc.GetMissEntrySize();

			desc.HitGroupTable.StartAddress = gpuAddress + generatorSize + missSize;
			desc.HitGroupTable.SizeInBytes = hitSize;
			desc.HitGroupTable.StrideInBytes = m_generalSBTDesc.GetHitGroupEntrySize();

			desc.CallableShaderTable.StartAddress = callableShadersSize > 0 ? gpuAddress + generatorSize + missSize + hitSize : 0;
			desc.CallableShaderTable.SizeInBytes = callableShadersSize;
			desc.CallableShaderTable.StrideInBytes = m_generalSBTDesc.GetMaxCallableShaderEntrySize();

			desc.Height = scissorRect.bottom - scissorRect.top;
			desc.Width = scissorRect.right - scissorRect.left;
			desc.Depth = 1;

			// Generate the ray traced image.
			commandList->SetPipelineState1(pipelineState);
			commandList->DispatchRays(&desc);
		});

		commandList->Close();
	}

	void Raytracing::AddObjectToAllTopLevelAS() {
		//for (auto it = storedObject->stages.begin(); it != storedObject->stages.end(); ++it) {
		//	if (it->type == DEPTH_STAGE && IsShadowEnabled()) {
		//		//shadowTlas.AddInstance(storedObject, it._Ptr);
		//	}


		//	// TODO: Add to all other instance lists.
		//}
	}

	ID3D12RootSignature* Raytracing::GetGlobalRootSignature() {
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		if (m_globalRootSignature.Get() == nullptr) {
			D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
			rootDesc.NumParameters = 0;
			rootDesc.pParameters = nullptr;
			rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ComPtr<ID3DBlob> rootSignature;
			ComPtr<ID3DBlob> error;

			DX12Rendering::ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignature, &error));

			DX12Rendering::ThrowIfFailed(device->CreateRootSignature(0, rootSignature->GetBufferPointer(), rootSignature->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature)));
		}

		return m_globalRootSignature.Get();
	}

	void Raytracing::CreateGlobalIlluminationPipeline()
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create the Pipline
		DX12Rendering::RaytracingPipeline pipeline(device, GetGlobalRootSignature());
		std::vector<std::wstring> generatorSymbols = { L"RayGen" };
		std::vector<std::wstring> missSymbols = { L"Miss" };
		std::vector<std::wstring> hitSymbols = { L"ClosestHit" };
		std::vector<std::wstring> allSymbols = { L"RayGen", L"Miss", L"ClosestHit" };

		pipeline.AddLibrary("globalillumination_generator", allSymbols);

		pipeline.AddAssocation(m_rayGenSignature.GetRootSignature(), generatorSymbols);
		pipeline.AddAssocation(m_missSignature.GetRootSignature(), missSymbols);
		pipeline.AddAssocation(m_hitSignature.GetRootSignature(), hitSymbols);

		pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");

		pipeline.SetMaxPayloadSize(9 * sizeof(float)); // diffuse, indirect, specular lighting.
		pipeline.SetMaxAttributeSize(4 * sizeof(float)); // x, y, z, w corrdinates.

		m_gliStateObject = pipeline.Generate();
		m_gliStateObject->SetName(L"DXR Global Illumination Pipeline State");

		// Copy the shader property data.
		ThrowIfFailed(m_gliStateObject->QueryInterface(IID_PPV_ARGS(&m_gliStateObjectProps)));
	}

	void Raytracing::CreateShadowPipeline() { 
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create the Pipline
		DX12Rendering::RaytracingPipeline pipeline(device, GetGlobalRootSignature());
		std::vector<std::wstring> generatorSymbols = { L"RayGen" };
		std::vector<std::wstring> missSymbols = { L"Miss" };
		std::vector<std::wstring> hitSymbols = { L"ClosestHit" };

		pipeline.AddLibrary("shadow_generator", generatorSymbols);
		pipeline.AddLibrary("general_miss", missSymbols);
		pipeline.AddLibrary("general_hit", hitSymbols);

		pipeline.AddAssocation(m_rayGenSignature.GetRootSignature(), generatorSymbols);
		pipeline.AddAssocation(m_missSignature.GetRootSignature(), missSymbols);
		pipeline.AddAssocation(m_hitSignature.GetRootSignature(), hitSymbols);

		pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
		
		pipeline.SetMaxPayloadSize(9 * sizeof(float)); // diffuse, indirect, specular lighting.
		pipeline.SetMaxAttributeSize(4 * sizeof(float)); // x, y, z, w corrdinates.

		m_shadowStateObject = pipeline.Generate();
		m_shadowStateObject->SetName(L"DXR Shadow Pipeline State");
		
		// Copy the shader property data.
		ThrowIfFailed(m_shadowStateObject->QueryInterface(IID_PPV_ARGS(&m_shadowStateObjectProps)));
	}

	void Raytracing::CreateShaderBindingTables()
	{
		for (UINT i = 0; i < DESCRIPTOR_OBJECT_TOTAL_FRAMES; ++i)
		{
			m_generalSBTData[i] = nullptr;
		}
	}

	ID3D12Resource* Raytracing::CreateShadowBindingTable(UINT frameIndex, UINT objectIndex, ID3D12StateObjectProperties* props)
	{
		assert(objectIndex < DESCRIPTOR_OBJECT_TOTAL);

		const UINT frameObjectIndex = (frameIndex * DESCRIPTOR_OBJECT_TOTAL) + objectIndex;

		if (m_generalSBTData[frameObjectIndex] == nullptr)
		{
			m_generalSBTDesc.Reset();

			D3D12_GPU_DESCRIPTOR_HANDLE srvUavHandle = GetGPUDescriptorHandle(frameIndex, objectIndex);
			D3D12_GPU_DESCRIPTOR_HANDLE textureHeap = GetDescriptorManager()->GetGPUDescriptorHandle(eHeapDescriptorTextureEntries, 0);
			D3D12_GPU_DESCRIPTOR_HANDLE textureConstantDescriptorHandle = GetDescriptorManager()->GetGPUDescriptorHandle(eHeapDescriptorTextureConstants, 0);
			D3D12_GPU_DESCRIPTOR_HANDLE samplerHeap = GetDescriptorManager()->GetGPUDescriptorHandle(eHeapDescriptorSamplerEntries, 0);

			std::vector<void*> heapPointers = {};
			heapPointers.push_back(reinterpret_cast<void*>(srvUavHandle.ptr));
			heapPointers.push_back(reinterpret_cast<void*>(textureHeap.ptr));
			heapPointers.push_back(reinterpret_cast<void*>(textureConstantDescriptorHandle.ptr));
			heapPointers.push_back(reinterpret_cast<void*>(samplerHeap.ptr));

			// TODO: We'll eventually need a custom SBT descriptor per object, but for now this will work with just shadows.

			// Create the SBT structure
			m_generalSBTDesc.AddRayGeneratorProgram(L"RayGen", heapPointers);
			m_generalSBTDesc.AddRayMissProgram(L"Miss", {});
			m_generalSBTDesc.AddRayHitGroupProgram(L"HitGroup", heapPointers);

			// Create the SBT resource
			const UINT32 tableSize = m_generalSBTDesc.CalculateTableSize();

			assert(tableSize != 0);

			// TODO: Update this to eventually work will all kinds of Raytracing shaders.
			auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(tableSize);

			ID3D12Device5* device = DX12Rendering::Device::GetDevice();

			// TODO: Change this to a system of placed resources.
			ThrowIfFailed(device->CreateCommittedResource(
				&kUploadHeapProps,
				D3D12_HEAP_FLAG_NONE,
				&resourceDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&(m_generalSBTData[frameObjectIndex]))
			));

			// Fill the SBT
			m_generalSBTDesc.Generate(m_generalSBTData[frameObjectIndex].Get(), props);
		}

		return m_generalSBTData[frameObjectIndex].Get();
	}

#ifdef DEBUG_IMGUI
	void Raytracing::ImGuiDebug()
	{
		if (ImGui::CollapsingHeader("Raytracing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			char fmtTitle[50];

			// BLAS information
			if (ImGui::CollapsingHeader("Bottom Level Acceleration Structor", ImGuiTreeNodeFlags_DefaultOpen))
			{
				m_blasManager.ImGuiDebug();
			}

			// TLAS information
			ImGui::CollapsingHeader("General TLAS", ImGuiTreeNodeFlags_DefaultOpen);
			m_tlasManager.ImGuiDebug();

			// Constant Information
			ImGui::CollapsingHeader("Shader Constants", ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::Text("RENDERPARM_GLOBALEYEPOS: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[0].x, m_constantBuffer.renderParameters[0].y, m_constantBuffer.renderParameters[0].z, m_constantBuffer.renderParameters[0].w);
			ImGui::Text("RENDERPARM_VIEWPORT: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[1].x, m_constantBuffer.renderParameters[1].y, m_constantBuffer.renderParameters[1].z, m_constantBuffer.renderParameters[1].w);
			ImGui::Text("RENDERPARAM_SCISSOR: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[2].x, m_constantBuffer.renderParameters[2].y, m_constantBuffer.renderParameters[2].z, m_constantBuffer.renderParameters[2].w);

			ImGui::Text("RENDERPARM_INVERSE_WORLDSPACE_X: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[3].x, m_constantBuffer.renderParameters[3].y, m_constantBuffer.renderParameters[3].z, m_constantBuffer.renderParameters[3].w);
			ImGui::Text("RENDERPARM_INVERSE_WORLDSPACE_Y: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[4].x, m_constantBuffer.renderParameters[4].y, m_constantBuffer.renderParameters[4].z, m_constantBuffer.renderParameters[4].w);
			ImGui::Text("RENDERPARM_INVERSE_WORLDSPACE_Z: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[5].x, m_constantBuffer.renderParameters[5].y, m_constantBuffer.renderParameters[5].z, m_constantBuffer.renderParameters[5].w);
			ImGui::Text("RENDERPARM_INVERSE_WORLDSPACE_W: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[6].x, m_constantBuffer.renderParameters[6].y, m_constantBuffer.renderParameters[6].z, m_constantBuffer.renderParameters[6].w);

			ImGui::Text("RENDERPARM_INVERSE_PROJMATRIX_X: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[7].x, m_constantBuffer.renderParameters[7].y, m_constantBuffer.renderParameters[7].z, m_constantBuffer.renderParameters[7].w);
			ImGui::Text("RENDERPARM_INVERSE_PROJMATRIX_Y: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[8].x, m_constantBuffer.renderParameters[8].y, m_constantBuffer.renderParameters[8].z, m_constantBuffer.renderParameters[8].w);
			ImGui::Text("RENDERPARM_INVERSE_PROJMATRIX_Z: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[9].x, m_constantBuffer.renderParameters[9].y, m_constantBuffer.renderParameters[9].z, m_constantBuffer.renderParameters[9].w);
			ImGui::Text("RENDERPARM_INVERSE_PROJMATRIX_W: %.1f, %.1f, %.1f, %.1f", m_constantBuffer.renderParameters[10].x, m_constantBuffer.renderParameters[10].y, m_constantBuffer.renderParameters[10].z, m_constantBuffer.renderParameters[10].w);
		}
	}
#endif
}