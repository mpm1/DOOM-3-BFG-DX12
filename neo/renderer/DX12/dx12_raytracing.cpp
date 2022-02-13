#pragma hdrstop

#include "./dx12_raytracing.h"

namespace DX12Rendering {
	// From NVIDIAs DXRHelper code
	// Specifies a heap used for uploading. This heap type has CPU access optimized
	// for uploading to the GPU.
	static const D3D12_HEAP_PROPERTIES kUploadHeapProps = {
		D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

	// Specifies the default heap. This heap type experiences the most bandwidth for
	// the GPU, but cannot provide CPU access.
	static const D3D12_HEAP_PROPERTIES kDefaultHeapProps = {
		D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

	ID3D12Resource* CreateBuffer(ID3D12Device5* device, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps) {
		D3D12_RESOURCE_DESC description = {};
		description.Alignment = 0;
		description.DepthOrArraySize = 1;
		description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		description.Flags = flags;
		description.Format = DXGI_FORMAT_UNKNOWN;
		description.Height = 1;
		description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		description.MipLevels = 1;
		description.SampleDesc.Count = 1;
		description.SampleDesc.Quality = 0;
		description.Width = size;

		ID3D12Resource* pBuffer;
		DX12ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &description, initState, nullptr, IID_PPV_ARGS(&pBuffer)));

		return pBuffer;
	}

	bool CheckRaytracingSupport(ID3D12Device5* device) {
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};

		if (!DX12WarnIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
			return false;
		}

		if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
			DX12WarnMessage("Raytracing Tier is not supported.");
			return false;
		}

		return true;
	}

	Raytracing::Raytracing(ID3D12Device5* device)
		: m_device(device),
		shadowTlas(device), reflectionTlas(device), emmisiveTlas(device),
		isRaytracingSupported(CheckRaytracingSupport(device)),
		m_rayGenSignature(device, WRITE_OUTPUT | READ_ENVIRONMENT),
		m_missSignature(device, NONE),
		m_hitSignature(device, NONE)
	{
		m_scratchBuffer = CreateBuffer(device, DEFAULT_SCRATCH_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
		CreatePipeline();
	}

	Raytracing::~Raytracing()
	{
	}

	void Raytracing::StartAccelerationStructure(bool raytracedShadows, bool raytracedReflections, bool raytracedIllumination) {
		m_state = BIT_RAYTRACED_NONE;

		if (raytracedShadows) {
			m_state |= BIT_RAYTRACED_SHADOWS;
			shadowTlas.Reset();
		}

		if (raytracedReflections) {
			m_state |= BIT_RAYTRACED_REFLECTIONS;
			reflectionTlas.Reset();
		}

		if (raytracedIllumination) {
			m_state |= BIT_RAYTRACED_ILLUMINATION;
			emmisiveTlas.Reset();
		}
	}

	void Raytracing::EndAccelerationStructure(ID3D12GraphicsCommandList4* commandList) {
		if (IsShadowEnabled()) {
			shadowTlas.UpdateResources(commandList, m_scratchBuffer.Get());
		}
		if (IsReflectiveEnabled()) {
			reflectionTlas.UpdateResources(commandList, m_scratchBuffer.Get());
		}
		if (IsIlluminationEnabled()) {
			emmisiveTlas.UpdateResources(commandList, m_scratchBuffer.Get());
		}

		// TODO: UpdateResources does not work yet for the TLAS, this is throwing a setup error.
			// Add matricies to the TLAS data
			// Add bone information to the TLAS data.
	}

	void Raytracing::CastRays(const CD3DX12_VIEWPORT viewport, const CD3DX12_RECT scissorRect) const {

	}

	void Raytracing::CacluateBLASBufferSizes(DX12Object* storedObject, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes) {
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE; //TODO: Right now all objects are considered new. Once we have objects cleaned up, we will change this to know which objects support updating.

		// Create the geometry descriptor
		D3D12_RAYTRACING_GEOMETRY_DESC descriptor = {};
		descriptor.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		descriptor.Triangles.VertexBuffer.StartAddress = storedObject->vertexBuffer->vertexBuffer->GetGPUVirtualAddress() + storedObject->vertexOffset;
		descriptor.Triangles.VertexBuffer.StrideInBytes = storedObject->vertexBuffer->vertexBufferView.StrideInBytes;
		//descriptor.Triangles.VertexCount = storedObject->vertexCount;
		descriptor.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		descriptor.Triangles.IndexBuffer = storedObject->indexBuffer->indexBuffer->GetGPUVirtualAddress() + storedObject->indexOffset;
		descriptor.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		descriptor.Triangles.IndexCount = storedObject->indexCount;
		descriptor.Triangles.Transform3x4 = 0; //TODO: Check if we need to add a transform here.
		descriptor.Flags = false ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
			: D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; //TODO: Eventually add support for opaque geometry.

		// Describe the BLAS data
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildDesc;
		prebuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		prebuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildDesc.NumDescs = 1;
		prebuildDesc.pGeometryDescs = &descriptor;
		prebuildDesc.Flags = flags;

		// Calculate the space needed to generate the Acceleration Structure
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		m_device->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildDesc, &info);

		// 256 Align the storage size.
		*scratchSizeInBytes = (info.ScratchDataSizeInBytes + 255) & ~255;
		*resultSizeInBytes = (info.ResultDataMaxSizeInBytes + 255) & ~255;
	}

	void Raytracing::AddObjectToAllTopLevelAS(DX12Object* storedObject, bool updateOnly) {
		for (auto it = storedObject->stages.begin(); it != storedObject->stages.end(); ++it) {
			if (it->type == DEPTH_STAGE && IsShadowEnabled()) {
				shadowTlas.AddInstance(storedObject, it._Ptr);
			}


			// TODO: Add to all other instance lists.
		}
	}

	void Raytracing::GenerateBottomLevelAS(ID3D12GraphicsCommandList4* commandList, DX12Object* storedObject, bool updateOnly) {
		assert(!updateOnly, "Updating a BLAS is currently not supported.");

		// Create the needed resource buffers.
		bool usedNewBuffer = UpdateBLASResources(storedObject, updateOnly);

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE; //TODO: Right now all objects are considered new. Once we have objects cleaned up, we will change this to know which objects support updating.

		// Create the geometry descriptor
		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.VertexBuffer.StartAddress = storedObject->vertexBuffer->vertexBuffer->GetGPUVirtualAddress() + storedObject->vertexOffset;
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = storedObject->vertexBuffer->vertexBufferView.StrideInBytes;
		geometryDesc.Triangles.VertexCount = storedObject->vertexCount;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geometryDesc.Triangles.IndexBuffer = storedObject->indexBuffer->indexBuffer->GetGPUVirtualAddress() + storedObject->indexOffset;
		geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometryDesc.Triangles.IndexCount = storedObject->indexCount;
		geometryDesc.Triangles.Transform3x4 = 0; //TODO: Check if we need to add a transform here.
		geometryDesc.Flags = false ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
			: D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; //TODO: Eventually add support for opaque geometry.

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = storedObject->blas->GetGPUVirtualAddress();

		// Describe the BLAS data
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC descriptor;
		descriptor.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		descriptor.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		descriptor.Inputs.NumDescs = 1;
		descriptor.Inputs.pGeometryDescs = &geometryDesc;
		descriptor.Inputs.Flags = flags;
		descriptor.DestAccelerationStructureData = {
			gpuAddress
		};
		descriptor.SourceAccelerationStructureData = updateOnly ? gpuAddress : 0;

		// Build the acceleration structure.
		commandList->BuildRaytracingAccelerationStructure(&descriptor, 0, nullptr);

		// Wait for the building of the blas to complete.
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = storedObject->blas.Get();
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		commandList->ResourceBarrier(1, &uavBarrier);
	}

	bool Raytracing::UpdateBLASResources(DX12Object* storedObject, bool updateOnly) {
		assert(!updateOnly || storedObject->blas != nullptr, "Previous result cannot be null while updating.");

		bool createdNewBuffer = false;
		UINT64 scratchSizeInBytes;
		UINT64 resultSizeInBytes;

		CacluateBLASBufferSizes(storedObject, &scratchSizeInBytes, &resultSizeInBytes);
		assert(scratchSizeInBytes < DEFAULT_SCRATCH_SIZE, "The generated objects scratch size is too large.");

		//if (storedObject->blas == nullptr) {
		createdNewBuffer = true;
		storedObject->blas = CreateBuffer(m_device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
		//}

		return createdNewBuffer;
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

			DX12ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignature, &error));

			DX12ThrowIfFailed(m_device->CreateRootSignature(0, rootSignature->GetBufferPointer(), rootSignature->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature)));
		}

		return m_globalRootSignature.Get();
	}

	void Raytracing::CreatePipeline() {
		// Create the Pipline
		DX12Rendering::RaytracingPipeline pipeline(m_device, "generator", GetGlobalRootSignature(), {L"RayGen", L"Miss", L"ClosestHit"});

		pipeline.AddAssocation(m_rayGenSignature.GetRootSignature(), { L"RayGen" });
		pipeline.AddAssocation(m_missSignature.GetRootSignature(), { L"Miss" });
		pipeline.AddAssocation(m_hitSignature.GetRootSignature(), { L"HitGroup" });

		pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
		
		pipeline.SetMaxPayloadSize(4 * sizeof(float)); //RGB + Distance

		// TODO: Add the hit and miss properties to the pipeline

		m_shadowStateObject = pipeline.Generate();
	}

	TopLevelAccelerationStructure::TopLevelAccelerationStructure(ID3D12Device5* device)
		: m_device(device)
	{

	}

	TopLevelAccelerationStructure::~TopLevelAccelerationStructure() {

	}

	void TopLevelAccelerationStructure::Reset() {
		m_instances.clear();
	}

	void TopLevelAccelerationStructure::AddInstance(DX12Object* storedObject, DX12Stage* stage) {
		Instance instance(storedObject->blas.Get());
		m_instances.push_back(instance);
	}

	void TopLevelAccelerationStructure::UpdateResources(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer) {
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = false
			? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
			: D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		UINT64 scratchSizeInBytes;
		UINT64 resultSizeInBytes;
		UINT64 instanceDescsSize;

		CacluateBufferSizes(&scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);
		assert(scratchSizeInBytes < DEFAULT_SCRATCH_SIZE, "The generated objects scratch size is too large.");

		if (instanceDescsSize == 0) {
			return;
		}

		if (m_result == nullptr) {
			m_result = CreateBuffer(m_device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
		}

		if (m_instanceDesc == nullptr) {
			m_instanceDesc = CreateBuffer(m_device, instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
		}

		D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = 0;// updateOnly ? previousResult->GetGPUVirtualAddress() : 0;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC descriptor = {};
		descriptor.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		descriptor.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		descriptor.Inputs.InstanceDescs = m_instanceDesc->GetGPUVirtualAddress();
		descriptor.Inputs.NumDescs = static_cast<UINT>(m_instances.size());
		descriptor.Inputs.Flags = flags;

		descriptor.DestAccelerationStructureData = {
			m_result->GetGPUVirtualAddress()
		};
		descriptor.ScratchAccelerationStructureData = {
			scratchBuffer->GetGPUVirtualAddress()
		};
		descriptor.SourceAccelerationStructureData = pSourceAS;

		// Build the top level AS
		commandList->BuildRaytracingAccelerationStructure(&descriptor, 0, nullptr);

		// Wait for the buffer to complete setup.
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = m_result.Get();
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		commandList->ResourceBarrier(1, &uavBarrier);
	}

	void TopLevelAccelerationStructure::CacluateBufferSizes(UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize) {
		const UINT numDescs = 1000;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = false
			? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
			: D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS description = {};
		description.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		description.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		description.NumDescs = numDescs; //static_cast<UINT>(m_instances.size());
		description.Flags = flags;

		// Calculate the space needed to generate the Acceleration Structure
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		m_device->GetRaytracingAccelerationStructurePrebuildInfo(&description, &info);

		// 256 Align the storage size.
		*scratchSizeInBytes = DX12_ALIGN(info.ScratchDataSizeInBytes, 256);
		*resultSizeInBytes = DX12_ALIGN(info.ResultDataMaxSizeInBytes, 256);

		*instanceDescsSize = DX12_ALIGN(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(m_instances.size()), 256);
	}
}