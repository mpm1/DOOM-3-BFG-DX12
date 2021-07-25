#pragma hdrstop

#include "./dx12_raytracing.h"

// From NVIDIAs DXRHelper code
// Specifies a heap used for uploading. This heap type has CPU access optimized
// for uploading to the GPU.
static const D3D12_HEAP_PROPERTIES kUploadHeapProps = {
	D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

// Specifies the default heap. This heap type experiences the most bandwidth for
// the GPU, but cannot provide CPU access.
static const D3D12_HEAP_PROPERTIES kDefaultHeapProps = {
	D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

DX12Raytracing::DX12Raytracing(ID3D12Device5* device)
	: m_device(device),
	isRaytracingSupported(CheckRaytracingSupport(device))
{
	m_scratchBuffer = CreateBuffer(DEFAULT_SCRATCH_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
}

DX12Raytracing::~DX12Raytracing()
{

}

void DX12Raytracing::CacluateBLASBufferSizes(DX12Object* storedObject, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes) {
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

void DX12Raytracing::GenerateBottomLevelAS(ID3D12GraphicsCommandList4* commandList, DX12Object* storedObject, bool updateOnly) {
	assert(!updateOnly, "Updating a BLAS is currently not supported.");

	// Create the needed resource buffers.
	UpdateBLASResources(storedObject, updateOnly);

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

	// Describe the BLAS data
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC descriptor;
	descriptor.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	descriptor.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	descriptor.Inputs.NumDescs = 1;
	descriptor.Inputs.pGeometryDescs = &geometryDesc;
	descriptor.Inputs.Flags = flags;
	descriptor.DestAccelerationStructureData = {
		storedObject->blas->GetGPUVirtualAddress()
	};
	descriptor.SourceAccelerationStructureData = updateOnly ? storedObject->blas->GetGPUVirtualAddress() : 0;

	// Build the acceleration structure.
	commandList->BuildRaytracingAccelerationStructure(&descriptor, 0, nullptr);

	// Wait for the building of the blas to complete.
	D3D12_RESOURCE_BARRIER uavBarrier;
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = storedObject->blas.Get();
	uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	commandList->ResourceBarrier(1, &uavBarrier);
}

void DX12Raytracing::UpdateBLASResources(DX12Object* storedObject, bool updateOnly) {
	assert(!updateOnly || storedObject->blas != nullptr, "Previous result cannot be null while updating.");

	UINT64 scratchSizeInBytes;
	UINT64 resultSizeInBytes;

	CacluateBLASBufferSizes(storedObject, &scratchSizeInBytes, &resultSizeInBytes);
	assert(scratchSizeInBytes < DEFAULT_SCRATCH_SIZE, "The generated objects scratch size is too large.");

	if (storedObject->blas == nullptr) {
		storedObject->blas = CreateBuffer(resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
	}
}

void DX12Raytracing::GenerateTopLevelAS(ID3D12GraphicsCommandList4* commandList, DX12Object* storedObject, DX12Stage* stage, bool updateOnly) {
	D3D12_RAYTRACING_INSTANCE_DESC* instanceDesc;
	stage->tlasInstanceDesc->Map(0, nullptr, reinterpret_cast<void**>(&instanceDesc));

	if (!instanceDesc) {
		DX12FailMessage("Cannot map instance descriptor for object.");
	}

	int i = 0;
}

void DX12Raytracing::UpdateTLASResources(DX12Object* storedObject, DX12Stage* stage) {
	UINT64 scratchSizeInBytes;
	UINT64 resultSizeInBytes;
	UINT64 instanceDescsSize;

	CacluateTLASBufferSizes(storedObject, stage, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);
	assert(scratchSizeInBytes < DEFAULT_SCRATCH_SIZE, "The generated objects scratch size is too large.");

	if (stage->tlas == nullptr) {
		stage->tlas = CreateBuffer(resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
	}

	if (stage->tlasInstanceDesc == nullptr) {
		stage->tlasInstanceDesc = CreateBuffer(instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
	}
}

void DX12Raytracing::CacluateTLASBufferSizes(DX12Object* storedObject, DX12Stage* stage, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize) {

}

bool DX12Raytracing::CheckRaytracingSupport(ID3D12Device5* device) {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};

	if(!DX12WarnIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))){
		return false;
	}

	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
		DX12WarnMessage("Raytracing Tier is not supported.");
		return false;
	}

	return true;
}

ID3D12Resource* DX12Raytracing::CreateBuffer(uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps) {
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
	DX12ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &description, initState, nullptr, IID_PPV_ARGS(&pBuffer)));

	return pBuffer;
}