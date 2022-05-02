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
		m_height(screenHeight)
	{
		const UINT64 scratchSize = DX12_ALIGN(DEFAULT_SCRATCH_SIZE, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
		scratchBuffer = CreateBuffer(
			device,
			scratchSize,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			kDefaultHeapProps);
		scratchBuffer->SetName(L"Raytracing Scratch Buffer");

		CreateCommandList();
		CreateShadowPipeline();
	}

	Raytracing::~Raytracing()
	{
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
		shadowDesc.Format = DXGI_FORMAT_R8_UINT;
		shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		shadowDesc.Width = GetShadowWidth();
		shadowDesc.Height = GetShadowHeight();
		shadowDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		shadowDesc.MipLevels = 1;
		shadowDesc.SampleDesc.Count = 1;

		DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &shadowDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&m_shadowResource)));
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
		// Create a SRV/UAV/CBV descriptor heap. We need 2 entries - 1 UAV for the 
		// raytracing output and 1 SRV for the TLAS 
		m_shadowUavHeaps = CreateDescriptorHeap(m_device, 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_shadowUavHeaps->GetCPUDescriptorHandleForHeapStart();

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		m_device->CreateUnorderedAccessView(m_shadowResource.Get(), nullptr, &uavDesc, shadowHandle);
	}

	//void Raytracing::StartAccelerationStructure(bool raytracedShadows, bool raytracedReflections, bool raytracedIllumination) 
	//{
	//	m_state = BIT_RAYTRACED_NONE;

	//	if (raytracedShadows) {
	//		m_state |= BIT_RAYTRACED_SHADOWS;
	//		shadowTlas.Reset();
	//	}

	//	if (raytracedReflections) {
	//		m_state |= BIT_RAYTRACED_REFLECTIONS;
	//		reflectionTlas.Reset();
	//	}

	//	if (raytracedIllumination) {
	//		m_state |= BIT_RAYTRACED_ILLUMINATION;
	//		emmisiveTlas.Reset();
	//	}
	//}

	//void Raytracing::EndAccelerationStructure() {
	//	if (IsShadowEnabled()) {
	//		shadowTlas.UpdateResources(m_commandList.Get(), scratchBuffer.Get());
	//	}
	//	if (IsReflectiveEnabled()) {
	//		reflectionTlas.UpdateResources(m_commandList.Get(), scratchBuffer.Get());
	//	}
	//	if (IsIlluminationEnabled()) {
	//		emmisiveTlas.UpdateResources(m_commandList.Get(), scratchBuffer.Get());
	//	}

	//	// TODO: UpdateResources does not work yet for the TLAS, this is throwing a setup error.
	//		// Add matricies to the TLAS data
	//		// Add bone information to the TLAS data.
	//}

	TopLevelAccelerationStructure* Raytracing::GetShadowTLAS(const dxHandle_t& handle)
	{
		try 
		{
			return &m_shadowTlas.at(handle);
		}
		catch (std::out_of_range e)
		{
			return nullptr;
		}
	}

	TopLevelAccelerationStructure* Raytracing::EmplaceShadowTLAS(const dxHandle_t& handle)
	{
		TopLevelAccelerationStructure* tlas = GetShadowTLAS(handle);

		if (tlas == nullptr) 
		{
			m_shadowTlas.emplace(handle, &blas);
			tlas = &m_shadowTlas.at(handle);
		}

		return tlas;
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
		D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_shadowUavHeaps->GetCPUDescriptorHandleForHeapStart();
		shadowHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = tlas->GetGPUVirtualAddress(); // Write the acceleration structure view in the heap 

		m_device->CreateShaderResourceView(m_shadowResource.Get(), &srvDesc, shadowHandle);
	}

	bool Raytracing::CastShadowRays(ID3D12GraphicsCommandList4* commandList, 
		const dxHandle_t lightHandle,
		const CD3DX12_VIEWPORT& viewport, 
		const CD3DX12_RECT& scissorRect, 
		ID3D12Resource* depthStencilBuffer,
		UINT32 stencilIndex)
	{
		DX12Rendering::TopLevelAccelerationStructure* tlas = GetShadowTLAS(lightHandle);

		if (tlas == nullptr || tlas->IsEmpty()) {
			// No objects to cast shadows.
			return false;
		}

		// TODO: Pass in the scissor rect into the ray generator. Outiside the rect will always return a ray miss.
		std::vector<ID3D12DescriptorHeap*> heaps = { m_shadowUavHeaps.Get() };
		commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

		// Transition the shadow rendering target to the unordered access state for rendering. We will then set it back to the copy state for copying.
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(m_shadowResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commandList->ResourceBarrier(1, &transition);

		// Create the ray dispatcher
		const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_shadowSBTData->GetGPUVirtualAddress();
		const UINT32 generatorSize = m_shadowSBTDesc.GetGeneratorSectorSize();
		const UINT32 missSize = m_shadowSBTDesc.GetMissSectorSize();
		const UINT32 hitSize = m_shadowSBTDesc.GetHitGroupSectorSize();

		D3D12_DISPATCH_RAYS_DESC desc = {};
		desc.RayGenerationShaderRecord.StartAddress = gpuAddress;
		desc.RayGenerationShaderRecord.SizeInBytes = generatorSize;

		desc.MissShaderTable.StartAddress = gpuAddress + generatorSize;
		desc.MissShaderTable.SizeInBytes = missSize;
		desc.MissShaderTable.StrideInBytes = m_shadowSBTDesc.GetMissEntrySize();

		desc.HitGroupTable.StartAddress = gpuAddress + generatorSize + missSize;
		desc.HitGroupTable.SizeInBytes = hitSize;
		desc.HitGroupTable.StrideInBytes = m_shadowSBTDesc.GetHitGroupEntrySize();

		desc.Height = viewport.Height;
		desc.Width = viewport.Width;
		desc.Depth = 1; 

		// Generate the ray traced image.
		commandList->SetPipelineState1(m_shadowStateObject.Get());
		commandList->DispatchRays(&desc);

		// Output to resource.
		// TODO: Copy this to the stencil buffer.
		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_shadowResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commandList->ResourceBarrier(1, &transition);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_DEST);
		commandList->ResourceBarrier(1, &transition);

		// TODO: Should we limit this to the scissor window?
		CD3DX12_TEXTURE_COPY_LOCATION dst(depthStencilBuffer, stencilIndex);
		CD3DX12_TEXTURE_COPY_LOCATION src(m_shadowResource.Get());
		commandList->CopyTextureRegion(&dst, viewport.TopLeftX, viewport.TopLeftY, 0, &src, nullptr);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		commandList->ResourceBarrier(1, &transition);

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

	bool Raytracing::IsReflectiveEnabled() const {
		return m_state & BIT_RAYTRACED_REFLECTIONS > 0;
	}

	bool Raytracing::IsShadowEnabled() const {
		return m_state & BIT_RAYTRACED_SHADOWS > 0;
	}

	bool Raytracing::IsIlluminationEnabled() const {
		return m_state & BIT_RAYTRACED_ILLUMINATION > 0;
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
		
		pipeline.SetMaxPayloadSize(4 * sizeof(float)); // Normal.xyz, and light amount.
		pipeline.SetMaxAttributeSize(4 * sizeof(float)); // x, y, z, w corrdinates.

		m_shadowStateObject = pipeline.Generate();
		
		// Copy the shader property data.
		ThrowIfFailed(m_shadowStateObject->QueryInterface(IID_PPV_ARGS(&m_shadowStateObjectProps)));
	}

	void Raytracing::CreateShaderBindingTables()
	{
		CreateShadowBindingTable();
	}

	void Raytracing::CreateShadowBindingTable()
	{
		m_shadowSBTDesc.Reset();
		D3D12_GPU_DESCRIPTOR_HANDLE srvUavHandle = m_shadowUavHeaps->GetGPUDescriptorHandleForHeapStart();
		void* heapPointer = reinterpret_cast<void*>(srvUavHandle.ptr);

		// Create the SBT structure
		m_shadowSBTDesc.AddRayGeneratorProgram(L"RayGen", { heapPointer });
		m_shadowSBTDesc.AddRayMissProgram(L"Miss", {});
		m_shadowSBTDesc.AddRayHitGroupProgram(L"HitGroup", {});

		// Create the SBT resource
		UINT32 tableSize = m_shadowSBTDesc.CalculateTableSize();
		ThrowIfFailed(m_device->CreateCommittedResource(
			&kUploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(tableSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_shadowSBTData)
		));

		// Fill the SBT
		m_shadowSBTDesc.Generate(m_shadowSBTData.Get(), m_shadowStateObjectProps.Get());
	}
}