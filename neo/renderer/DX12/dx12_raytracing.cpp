#pragma hdrstop

#include <stdexcept>
#include <algorithm>

#include "../tr_local.h"

#include "./dx12_raytracing.h"
#include "./dx12_DeviceManager.h"
#include "./dx12_RenderPass.h"

idCVar s_raysCastPerLight("s_raysCastPerLight", "20", CVAR_ARCHIVE | CVAR_INTEGER, "number of shadow rays per light per pixel.", 0, 1000);
idCVar s_lightEmissiveRadius("s_lightEmissiveRadius", "20.0", CVAR_ARCHIVE | CVAR_FLOAT, "the radius of a light. The larger the value, the softer shadows will be.", 0.0f, 100.0f);

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
		m_rayGenSignature( WRITE_OUTPUT | READ_ENVIRONMENT),
		m_missSignature(NONE),
		m_hitSignature(NONE),
		m_width(screenWidth),
		m_height(screenHeight),
		m_tlasManager(&m_blasManager),
		m_nextDescriptorHeapIndex(0),
		m_constantBuffer({})
		//m_localVertexBuffer(VERTCACHE_VERTEX_MEMORY),
		//m_localIndexBuffer(VERTCACHE_INDEX_MEMORY)
	{
		CreateShadowPipeline();
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

		if (UINT count = GetBLASManager()->Generate() > 0)
		{
			//TODO: Move this to a more generalized execution tract.
			//commandList->Cycle();
		}

		CleanUpAccelerationStructure();
	}

	void Raytracing::EndFrame()
	{
		
	}

	void Raytracing::CreateCBVHeap(const size_t constantBufferSize)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		m_cbvHeapIncrementor = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC Raytracing::SetCBVDescriptorTable(const size_t constantBufferSize, const void* constantBuffer, const UINT frameIndex) {
		DX12Rendering::ResourceManager& resourceManager = *DX12Rendering::GetResourceManager();

		// Copy the CBV value to the upload heap
		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(constantBufferSize);
		resourceManager.FillConstantBuffer(buffer, constantBuffer);

		// Create the constant buffer view for the object
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart(), DX12Rendering::e_RaytracingHeapIndex::CBV_CameraProperties, m_cbvHeapIncrementor);

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		device->CreateConstantBufferView(&buffer.bufferLocation, descriptorHandle);

		return buffer.bufferLocation;
	}

	void Raytracing::CreateOutputBuffers()
	{
		//TODO: Make shadow atlas for lights. Right now were putting the depth into the diffuse buffer for testing
	}

	void Raytracing::Resize(UINT width, UINT height, DX12Rendering::TextureManager &textureManager)
	{
		m_width = width;
		m_height = height;

		DX12Rendering::GetSurface(eRenderSurface::RaytraceShadowMask)->Resize(width, height);

		// TODO: Check if the buffers already exists and clear them
		CreateOutputBuffers();
		CreateShaderResourceHeap(textureManager);
		CreateShaderBindingTables();
	}

	void Raytracing::SetOutputTexture(DX12Rendering::eRenderSurface renderSurface, DX12Rendering::e_RaytracingHeapIndex uav)
	{
		RenderSurface* duffuseSurface = DX12Rendering::GetSurface(renderSurface);
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart(), uav, m_cbvHeapIncrementor);

		duffuseSurface->CreateUnorderedAccessView(uavHandle);
	}

	void Raytracing::CreateShaderResourceHeap(DX12Rendering::TextureManager& textureManager)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create a SRV/UAV/CBV descriptor heap. We need 4 entries + texture entries - 
		// 3 UAV for the raytracing output
		// 1 SRV for the TLAS
		// 1 CBV for the Camera properties
		// 1024 SRV for textures.
		m_generalUavHeaps = CreateDescriptorHeap(device, DESCRIPTOR_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart();

		// Add the output buffers
		SetOutputTexture(eRenderSurface::RaytraceShadowMask, e_RaytracingHeapIndex::UAV_ShadowMap); // TODO: Remove
		SetOutputTexture(eRenderSurface::Diffuse, e_RaytracingHeapIndex::UAV_DiffuseMap);
		SetOutputTexture(eRenderSurface::Specular, e_RaytracingHeapIndex::UAV_SpecularMap);
	}

	void Raytracing::CleanUpAccelerationStructure()
	{
		// TODO: Add any acceleration structure cleanup.
	}

	void Raytracing::GenerateTLAS()
	{
		// Update the resource
		if (!m_tlasManager.Generate())
		{
			return;
		}

		if (!m_tlasManager.IsDirty())
		{
			m_tlasManager.Reset(ACCELERATION_INSTANCE_TYPE::INSTANCE_TYPE_STATIC | ACCELERATION_INSTANCE_TYPE::INSTANCE_TYPE_DYNAMIC);
		}

		if (m_tlasManager.GetCurrent().state != Resource::eResourceState::Ready)
		{
			return;
		}

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Write the acceleration structure to the view.
		CD3DX12_CPU_DESCRIPTOR_HANDLE shadowHandle(GetUavHeap()->GetCPUDescriptorHandleForHeapStart(), DX12Rendering::e_RaytracingHeapIndex::SRV_TLAS, m_cbvHeapIncrementor);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_tlasManager.GetCurrent().resource->GetGPUVirtualAddress(); // Write the acceleration structure view in the heap 

		device->CreateShaderResourceView(nullptr, &srvDesc, shadowHandle);
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
		for (int index = 0; index < m_constantBuffer.lightCount; ++index)
		{
			if (m_constantBuffer.lights[index].lightIndex == lightIndex)
			{
				return 0x00000001 << index;
			}
		}

		return 0x00000000;
	}

	UINT Raytracing::AddImageToDescriptorHeap(const DX12Rendering::TextureBuffer* texture)
	{
		if (m_nextDescriptorHeapIndex >= DX12Rendering::DESCRIPTOR_TEXTURE_COUNT)
		{
			return 0;
		}

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		if (device == nullptr)
		{
			return 0;
		}

		const UINT index = m_nextDescriptorHeapIndex;
		++m_nextDescriptorHeapIndex;

		CD3DX12_CPU_DESCRIPTOR_HANDLE textureHandle(m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart(), index + DX12Rendering::e_RaytracingHeapIndex::SRV_TextureArray /* starting point */, m_cbvHeapIncrementor);
		device->CreateShaderResourceView(texture->resource.Get(), &texture->textureView, textureHandle);

		return index;
	}

	bool Raytracing::AddLight(const UINT lightIndex, const DXR_LIGHT_TYPE type, const DX12Rendering::TextureBuffer* falloffTexture, const DX12Rendering::TextureBuffer* projectionTexture, const UINT shadowMask, const XMFLOAT4 location, XMFLOAT4 color, const XMFLOAT4 lightProjection[4], const XMFLOAT4 scissorWindow, bool castsShadows)
	{
		static UINT padValue = 0;
		padValue = (padValue + 1) % 3459871;

		UINT index = m_constantBuffer.lightCount;
		if(index >= MAX_SCENE_LIGHTS)
		{ 
			//assert(false, "Raytracing::AddLight: Too many lights.");
			return false;
		}

		m_constantBuffer.lights[index].falloffIndex = 0;
		m_constantBuffer.lights[index].projectionIndex = 0;

		m_constantBuffer.lights[index].flags = 0;

		m_constantBuffer.lights[index].castsShadows = castsShadows;

		{
			// Calculate type properties
			if ((type & DXR_LIGHT_TYPE_AMBIENT) > 0)
			{
				m_constantBuffer.lights[index].isAmbientLight = true;
			}
			else
			{
				// All lights that are not ambient are point lights
				m_constantBuffer.lights[index].isPointLight = true;
			}

			if ((type & DXR_LIGHT_TYPE_FOG) > 0)
			{
				m_constantBuffer.lights[index].isFogLight = true;
			}
		}

		m_constantBuffer.lights[index].lightIndex = lightIndex;
		m_constantBuffer.lights[index].shadowMask = shadowMask;

		m_constantBuffer.lights[index].emissiveRadius = s_lightEmissiveRadius.GetFloat();

		m_constantBuffer.lights[index].center = location;

		m_constantBuffer.lights[index].scissor = scissorWindow;

		m_constantBuffer.lights[index].color = color;

		m_constantBuffer.lights[index].projectionS = lightProjection[0];
		m_constantBuffer.lights[index].projectionT = lightProjection[1];
		m_constantBuffer.lights[index].projectionQ = lightProjection[2];
		m_constantBuffer.lights[index].falloffS = lightProjection[3];

		m_constantBuffer.lights[index].falloffIndex = AddImageToDescriptorHeap(falloffTexture);
		m_constantBuffer.lights[index].projectionIndex = AddImageToDescriptorHeap(projectionTexture);

		++m_constantBuffer.lightCount;

		return true;
	}

	bool Raytracing::CastShadowRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect,
		DX12Rendering::TextureManager* textureManager
	)
	{
		const UINT surfaceCount = 3;
		const DX12Rendering::eRenderSurface surfaces[surfaceCount] = {
			DX12Rendering::eRenderSurface::RaytraceShadowMask,
			DX12Rendering::eRenderSurface::Diffuse,
			DX12Rendering::eRenderSurface::Specular
		};

		assert(textureManager);

		auto tlasManager = GetTLASManager();

		if (tlasManager && !tlasManager->IsReady()) {
			// No objects to cast shadows.
			return false;
		}

		//tlasManager->WaitForFence();

		auto commandManager = DX12Rendering::Commands::GetCommandManager(Commands::COMPUTE);
		DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RayTracing::CastShadowRays");

		//Update the resources
		m_constantBuffer.depthTextureIndex = AddImageToDescriptorHeap(textureManager->GetGlobalTexture(eGlobalTexture::VIEW_DEPTH));
		m_constantBuffer.flatNormalIndex = AddImageToDescriptorHeap(textureManager->GetGlobalTexture(eGlobalTexture::WORLD_FLAT_NORMALS));
		m_constantBuffer.normalIndex = AddImageToDescriptorHeap(textureManager->GetGlobalTexture(eGlobalTexture::WORLD_NORMALS));
		m_constantBuffer.diffuseTextureIndex = AddImageToDescriptorHeap(textureManager->GetGlobalTexture(eGlobalTexture::ALBEDO));
		m_constantBuffer.specularTextureIndex = AddImageToDescriptorHeap(textureManager->GetGlobalTexture(eGlobalTexture::SPECULAR_COLOR));

		m_constantBuffer.raysPerLight = s_raysCastPerLight.GetInteger();

		// Update the noise offset
		m_constantBuffer.noiseOffset += 0.01f;
		if (m_constantBuffer.noiseOffset > 5.0f)
		{
			m_constantBuffer.noiseOffset = 0.0f;
		}

		return CastRays(frameIndex, viewport, scissorRect, surfaces, surfaceCount);
	}

	bool Raytracing::CastRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect,
		const DX12Rendering::eRenderSurface* renderTargetList, 
		const UINT renderTargetCount
	)
	{
		DX12Rendering::RenderPassBlock renderPass(
			"RayTracing::CastRays",
			Commands::COMPUTE,
			renderTargetList,
			renderTargetCount
		);
		auto commandList = renderPass.GetCommandManager()->RequestNewCommandList();

		commandList->AddPreFenceWait(&m_tlasManager.GetCurrent().fence);

		// Copy the CBV data to the heap
		float scissorVector[4] = { scissorRect.left, scissorRect.top, scissorRect.right, scissorRect.bottom };
		Uniform4f(RENDERPARAM_SCISSOR, scissorVector);

		float viewportVector[4] = { viewport.TopLeftX, viewport.TopLeftY, viewport.TopLeftX + viewport.Width, viewport.TopLeftY + viewport.Height };
		Uniform4f(RENDERPARM_VIEWPORT, viewportVector);

		SetCBVDescriptorTable(sizeof(m_constantBuffer), &m_constantBuffer, frameIndex);
		
		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			std::vector<ID3D12DescriptorHeap*> heaps = { GetUavHeap() };
			commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

			// Create the ray dispatcher
			const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_generalSBTData->GetGPUVirtualAddress();
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

			desc.CallableShaderTable.StartAddress = gpuAddress + generatorSize + missSize + hitSize;
			desc.CallableShaderTable.SizeInBytes = callableShadersSize;
			desc.CallableShaderTable.StrideInBytes = m_generalSBTDesc.GetCallableShaderSectorSize();

			desc.Height = viewport.Height;
			desc.Width = viewport.Width;
			desc.Depth = 1;

			// Generate the ray traced image.
			commandList->SetPipelineState1(m_shadowStateObject.Get());
			commandList->DispatchRays(&desc);
		});

		commandList->Close();

		return true;
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
		CreateShadowBindingTable();
	}

	void Raytracing::CreateShadowBindingTable()
	{
		m_generalSBTDesc.Reset();

		D3D12_GPU_DESCRIPTOR_HANDLE srvUavHandle = m_generalUavHeaps->GetGPUDescriptorHandleForHeapStart();
		void* heapPointer = reinterpret_cast<void*>(srvUavHandle.ptr);

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create the SBT structure
		m_generalSBTDesc.AddRayGeneratorProgram(L"RayGen", { heapPointer } );
		m_generalSBTDesc.AddRayMissProgram(L"Miss", {});
		m_generalSBTDesc.AddRayHitGroupProgram(L"HitGroup", {});

		// Create the SBT resource
		const UINT32 tableSize = m_generalSBTDesc.CalculateTableSize();

		assert(tableSize != 0);

		ThrowIfFailed(device->CreateCommittedResource(
			&kUploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(tableSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_generalSBTData)
		));

		// Fill the SBT
		m_generalSBTDesc.Generate(m_generalSBTData.Get(), m_shadowStateObjectProps.Get());
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