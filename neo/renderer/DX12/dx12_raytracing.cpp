#pragma hdrstop

#include <stdexcept>
#include "./dx12_raytracing.h"

namespace DX12Rendering {
	bool CheckRaytracingSupport(ID3D12Device5* device) {
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

	Raytracing::Raytracing(ID3D12Device5* device, UINT screenWidth, UINT screenHeight)
		: m_device(device),
		isRaytracingSupported(CheckRaytracingSupport(device)),
		m_rayGenSignature(device, WRITE_OUTPUT | READ_ENVIRONMENT),
		m_missSignature(device, NONE),
		m_hitSignature(device, NONE),
		m_width(screenWidth),
		m_height(screenHeight),
		m_staticTlas(&m_blas),
		m_localVertexBuffer(VERTCACHE_VERTEX_MEMORY),
		m_localIndexBuffer(VERTCACHE_INDEX_MEMORY)
	{
		const UINT64 scratchSize = DX12_ALIGN(DEFAULT_SCRATCH_SIZE, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
		scratchBuffer = CreateBuffer(
			device,
			scratchSize,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			kDefaultHeapProps);
		scratchBuffer->SetName(L"Raytracing Scratch Buffer");

		m_fence.Allocate(m_device);

		CreateCommandList();
		CreateShadowPipeline();
		CreateCBVHeap(sizeof(m_constantBuffer));

		Signal();
	}

	Raytracing::~Raytracing()
	{
	}

	void Raytracing::CreateCBVHeap(const size_t constantBufferSize)
	{
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
				DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(
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

			m_cbvHeapIncrementor = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC Raytracing::SetCBVDescriptorTable(ID3D12GraphicsCommandList* commandList, const size_t constantBufferSize, const XMFLOAT4* constantBuffer, const UINT frameIndex) {
		// Copy the CBV value to the upload heap
		UINT8* buffer;
		const UINT bufferSize = ((constantBufferSize + 255) & ~255);
		const UINT offset = 0; // Each entry is 256 byte aligned.
		CD3DX12_RANGE readRange(offset, bufferSize);

		DX12Rendering::ThrowIfFailed(m_cbvUploadHeap[frameIndex]->Map(0, &readRange, reinterpret_cast<void**>(&buffer)));
		memcpy(&buffer[offset], constantBuffer, constantBufferSize);
		m_cbvUploadHeap[frameIndex]->Unmap(0, &readRange);

		// Create the constant buffer view for the object
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart(), 2, m_cbvHeapIncrementor);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_cbvUploadHeap[frameIndex]->GetGPUVirtualAddress() + offset;
		cbvDesc.SizeInBytes = bufferSize;
		m_device->CreateConstantBufferView(&cbvDesc, descriptorHandle);

		// Define the Descriptor Table to use.
		const CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorTableHandle(m_generalUavHeaps->GetGPUDescriptorHandleForHeapStart(), 2, m_cbvHeapIncrementor);
		commandList->SetGraphicsRootDescriptorTable(0, descriptorTableHandle);

		return cbvDesc;
	}

	void Raytracing::CreateCommandList()
	{
		// Describe and create the command queue
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

		ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
		m_commandQueue->SetName(L"Raytracing Command Queue");

		// Create the Command Allocator
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_commandAllocator)));
		m_commandAllocator->SetName(L"Raytracing Command Allocator");

		//Create the command list
		DX12Rendering::ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_commandAllocator.Get(), NULL, IID_PPV_ARGS(&m_commandList)));
		DX12Rendering::ThrowIfFailed(m_commandList->Close());
		m_commandList->SetName(L"Raytracing Command List");
	}

	void Raytracing::ExecuteCommandList()
	{
		WarnIfFailed(m_commandList->Close());

		ID3D12CommandList* const ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	}

	void Raytracing::ResetCommandList()
	{
		ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), NULL));
	}

	void Raytracing::ResetCommandAllocator()
	{
		ThrowIfFailed(m_commandAllocator->Reset());
	}

	void Raytracing::ResetFrame()
	{
		ResetCommandAllocator();
		ResetCommandList();
	}

	void Raytracing::CreateOutputBuffers()
	{
		// Create the resource to draw raytraced shadows to.
		D3D12_RESOURCE_DESC shadowDesc = {};
		shadowDesc.DepthOrArraySize = 1;
		shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		shadowDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //DXGI_FORMAT_R8_UINT;
		shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		shadowDesc.Width = GetShadowWidth();
		shadowDesc.Height = GetShadowHeight();
		shadowDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		shadowDesc.MipLevels = 1;
		shadowDesc.SampleDesc.Count = 1;

		DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &shadowDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&m_diffuseResource)));
		m_diffuseResource->SetName(L"DXR Shadow Output");
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
		// Create a SRV/UAV/CBV descriptor heap. We need 3 entries - 
		// 1 UAV for the raytracing output
		// 1 SRV for the TLAS 
		// 1 CBV for the Camera properties
		m_generalUavHeaps = CreateDescriptorHeap(m_device, 3, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart();

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		m_device->CreateUnorderedAccessView(m_diffuseResource.Get(), nullptr, &uavDesc, shadowHandle);
	}

	void Raytracing::ResetGeneralTLAS()
	{
		m_staticTlas.Reset();
	}

	void Raytracing::CleanUpAccelerationStructure()
	{
		// TODO: Add any acceleration structure cleanup.
	}

	void Raytracing::GenerateTLAS(DX12Rendering::TopLevelAccelerationStructure* tlas)
	{
		assert(tlas != nullptr);

		// Update the resource
		if (!tlas->UpdateResources(m_device, m_commandList.Get(), scratchBuffer.Get(), DEFAULT_SCRATCH_SIZE))
		{
			return;
		}

		// Write the acceleration structure to the view.
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_generalUavHeaps->GetCPUDescriptorHandleForHeapStart();
		shadowHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress(); // Write the acceleration structure view in the heap 

		m_device->CreateShaderResourceView(nullptr, &srvDesc, shadowHandle);
	}

	void Raytracing::Uniform4f(dxr_renderParm_t param, const float* uniform)
	{
		memcpy(&m_constantBuffer[param], uniform, sizeof(XMFLOAT4));
	}

	bool Raytracing::CastRays(ID3D12GraphicsCommandList4* commandList,
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect
	)
	{
		m_fence.Wait();

		DX12Rendering::TopLevelAccelerationStructure* tlas = GetGeneralTLAS();

		if (tlas == nullptr || tlas->IsReadEmpty()) {
			// No objects to cast shadows.
			return false;
		}

		// TODO: Pass in the scissor rect into the ray generator. Outiside the rect will always return a ray miss.

		// Copy the CBV data to the heap
		float scissorVector[4] = { scissorRect.left, scissorRect.top, scissorRect.right, scissorRect.bottom };
		Uniform4f(RENDERPARAM_SCISSOR, scissorVector);

		float viewportVector[4] = { viewport.TopLeftX, viewport.TopLeftY, viewport.TopLeftX + viewport.Width, viewport.TopLeftY + viewport.Height };
		Uniform4f(RENDERPARM_VIEWPORT, viewportVector);

		SetCBVDescriptorTable(commandList, sizeof(m_constantBuffer), m_constantBuffer, frameIndex);

		std::vector<ID3D12DescriptorHeap*> heaps = { m_generalUavHeaps.Get() };
		commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

		// Transition the shadow rendering target to the unordered access state for rendering. We will then set it back to the copy state for copying.
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_diffuseResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commandList->ResourceBarrier(1, &transition);

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
		
		m_fence.Signal(m_device, m_commandQueue.Get());

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
		if (m_globalRootSignature.Get() == nullptr) {
			D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
			rootDesc.NumParameters = 0;
			rootDesc.pParameters = nullptr;
			rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

			ComPtr<ID3DBlob> rootSignature;
			ComPtr<ID3DBlob> error;

			DX12Rendering::ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignature, &error));

			DX12Rendering::ThrowIfFailed(m_device->CreateRootSignature(0, rootSignature->GetBufferPointer(), rootSignature->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature)));
		}

		return m_globalRootSignature.Get();
	}

	void Raytracing::CreateShadowPipeline() { 
		// Create the Pipline
		DX12Rendering::RaytracingPipeline pipeline(m_device, GetGlobalRootSignature());
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

		// Create the SBT structure
		m_generalSBTDesc.AddRayGeneratorProgram(L"RayGen", { heapPointer });
		m_generalSBTDesc.AddRayMissProgram(L"Miss", {});
		m_generalSBTDesc.AddRayHitGroupProgram(L"HitGroup", {});

		// Create the SBT resource
		UINT32 tableSize = m_generalSBTDesc.CalculateTableSize();
		ThrowIfFailed(m_device->CreateCommittedResource(
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
				m_blas.ImGuiDebug();
			}

			// TLAS information
			ImGui::CollapsingHeader("General TLAS", ImGuiTreeNodeFlags_DefaultOpen);
			m_staticTlas.ImGuiDebug();

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