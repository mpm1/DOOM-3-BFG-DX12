#pragma hdrstop

#include <stdexcept>
#include "./dx12_raytracing.h"
#include "./dx12_DeviceManager.h"

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
		m_diffuseTarget(DXGI_FORMAT_R8G8B8A8_UNORM, L"DXR Diffuse Map")//,
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
		
	}

	void Raytracing::CreateCBVHeap(const size_t constantBufferSize)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create the buffer size.
		constexpr UINT resourceAlignment = (1024 * 64) - 1; // Resource must be a multible of 64KB
		const UINT entrySize = (constantBufferSize + 255) & ~255; // Size is required to be 256 byte aligned
		const UINT heapSize = ((entrySize * MAX_OBJECT_COUNT) + resourceAlignment) & ~resourceAlignment;
		WCHAR heapName[30];

		// Create Descriptor Heaps
		{
			// Describe and create the constant buffer view (CBV) descriptor for each frame
			for (UINT frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex) {
				// Create the Constant buffer heap for each frame
				DX12Rendering::ThrowIfFailed(device->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
					D3D12_HEAP_FLAG_NONE,
					&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr, // Currently not clear value needed
					IID_PPV_ARGS(&m_cbvUploadHeap[frameIndex])
				));

				wsprintfW(heapName, L"Raytracing CBV Upload Heap %d", frameIndex);
				m_cbvUploadHeap[frameIndex]->SetName(heapName);
			}

			m_cbvHeapIncrementor = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC Raytracing::SetCBVDescriptorTable(const size_t constantBufferSize, const XMFLOAT4* constantBuffer, const UINT frameIndex) {
		// Copy the CBV value to the upload heap
		UINT8* buffer;
		const UINT bufferSize = ((constantBufferSize + 255) & ~255);
		const UINT offset = 0; // Each entry is 256 byte aligned.
		CD3DX12_RANGE readRange(offset, bufferSize);

		DX12Rendering::ThrowIfFailed(m_cbvUploadHeap[frameIndex]->Map(0, &readRange, reinterpret_cast<void**>(&buffer)));
		memcpy(buffer, constantBuffer, constantBufferSize);
		m_cbvUploadHeap[frameIndex]->Unmap(0, &readRange);

		// Create the constant buffer view for the object
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart(), 2, m_cbvHeapIncrementor);

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_cbvUploadHeap[frameIndex]->GetGPUVirtualAddress() + offset;
		cbvDesc.SizeInBytes = bufferSize;
		device->CreateConstantBufferView(&cbvDesc, descriptorHandle);

		// Define the Descriptor Table to use.
		auto commandList = Commands::GetCommandList(Commands::COMPUTE);

		/*commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			const CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorTableHandle(m_generalUavHeaps->GetGPUDescriptorHandleForHeapStart(), 2, m_cbvHeapIncrementor);
			commandList->SetGraphicsRootDescriptorTable(0, descriptorTableHandle);
		});*/

		return cbvDesc;
	}

	void Raytracing::CreateOutputBuffers()
	{
		//TODO: Make shadow atlas for lights. Right now were putting the depth into the diffuse buffer for testing
		m_diffuseTarget.Resize(GetShadowWidth(), GetShadowHeight());
	}

	void Raytracing::Resize(UINT width, UINT height)
	{
		m_width = width;
		m_height = height;

		// TODO: Check if the buffers already exists and clear them
		CreateOutputBuffers();
		CreateShaderResourceHeap();
		CreateShaderBindingTables();
	}

	void Raytracing::CreateShaderResourceHeap()
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Create a SRV/UAV/CBV descriptor heap. We need 3 entries - 
		// 1 UAV for the raytracing output
		// 1 SRV for the TLAS 
		// 1 CBV for the Camera properties
		m_generalUavHeaps = CreateDescriptorHeap(device, 3, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart();

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device->CreateUnorderedAccessView(m_diffuseTarget.resource.Get(), nullptr, &uavDesc, shadowHandle);
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

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Write the acceleration structure to the view.
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart();
		shadowHandle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_tlasManager.GetCurrent().resource->GetGPUVirtualAddress(); // Write the acceleration structure view in the heap 

		device->CreateShaderResourceView(nullptr, &srvDesc, shadowHandle);
	}

	void Raytracing::Uniform4f(dxr_renderParm_t param, const float* uniform)
	{
		memcpy(&m_constantBuffer[param], uniform, sizeof(XMFLOAT4));
	}

	bool Raytracing::CastRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect
	)
	{
		auto tlasManager = GetTLASManager();

		if (!tlasManager->IsReady()) {
			// No objects to cast shadows.
			return false;
		}

		auto commandList = DX12Rendering::Commands::GetCommandList(Commands::COMPUTE);
		DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList);
		DX12Rendering::CaptureEventBlock captureEvent(commandList, "RayTracing::CastRays");

		// TODO: Pass in the scissor rect into the ray generator. Outiside the rect will always return a ray miss.

		//tlasManager->AddGPUWait(commandList);

		// Copy the CBV data to the heap
		float scissorVector[4] = { scissorRect.left, scissorRect.top, scissorRect.right, scissorRect.bottom };
		Uniform4f(RENDERPARAM_SCISSOR, scissorVector);

		float viewportVector[4] = { viewport.TopLeftX, viewport.TopLeftY, viewport.TopLeftX + viewport.Width, viewport.TopLeftY + viewport.Height };
		Uniform4f(RENDERPARM_VIEWPORT, viewportVector);

		SetCBVDescriptorTable(sizeof(m_constantBuffer), m_constantBuffer, frameIndex);

		
		commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			std::vector<ID3D12DescriptorHeap*> heaps = { m_generalUavHeaps.Get() };
			commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

			// Transition the shadow rendering target to the unordered access state for rendering. We will then set it back to the copy state for copying.
			D3D12_RESOURCE_BARRIER transition = {};
			if (m_diffuseTarget.TryTransition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &transition))
			{
				commandList->ResourceBarrier(1, &transition);
			}

			// Create the ray dispatcher
			const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_generalSBTData->GetGPUVirtualAddress();
			const UINT32 generatorSize = m_generalSBTDesc.GetGeneratorSectorSize();
			const UINT32 missSize = m_generalSBTDesc.GetMissSectorSize();
			const UINT32 hitSize = m_generalSBTDesc.GetHitGroupSectorSize();

			D3D12_DISPATCH_RAYS_DESC desc = {};
			desc.RayGenerationShaderRecord.StartAddress = gpuAddress;
			desc.RayGenerationShaderRecord.SizeInBytes = generatorSize;

			desc.MissShaderTable.StartAddress = gpuAddress + generatorSize;
			desc.MissShaderTable.SizeInBytes = missSize;
			desc.MissShaderTable.StrideInBytes = m_generalSBTDesc.GetMissEntrySize();

			desc.HitGroupTable.StartAddress = gpuAddress + generatorSize + missSize;
			desc.HitGroupTable.SizeInBytes = hitSize;
			desc.HitGroupTable.StrideInBytes = m_generalSBTDesc.GetHitGroupEntrySize();

			desc.Height = viewport.Height;
			desc.Width = viewport.Width;
			desc.Depth = 1;

			// Generate the ray traced image.
			commandList->SetPipelineState1(m_shadowStateObject.Get());
			commandList->DispatchRays(&desc);

			m_diffuseTarget.fence.Signal(DX12Rendering::Device::GetDevice(), commandQueue);
		});

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
		m_generalSBTDesc.AddRayGeneratorProgram(L"RayGen", { heapPointer });
		m_generalSBTDesc.AddRayMissProgram(L"Miss", {});
		m_generalSBTDesc.AddRayHitGroupProgram(L"HitGroup", {});

		// Create the SBT resource
		UINT32 tableSize = m_generalSBTDesc.CalculateTableSize();
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
			for (int i = 0; i < dxr_renderParm_t::COUNT; ++i)
			{
				ImGui::Text("%s: %f, %f, %f, %f", "TODO", m_constantBuffer[i].x, m_constantBuffer[i].y, m_constantBuffer[i].z, m_constantBuffer[i].w);
			}
		}
	}
#endif
}