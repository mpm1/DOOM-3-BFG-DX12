#pragma hdrstop

#include "./dx12_raytracing.h"

DX12Raytracing::DX12Raytracing(ID3D12Device5* device)
	: m_device(device),
	isRaytracingSupported(CheckRaytracingSupport(device))
{
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
	descriptor.Triangles.VertexCount = storedObject->vertexCount;
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
	*scratchSizeInBytes = (info.ScratchDataSizeInBytes + 255) & ~256;
	*resultSizeInBytes = (info.ResultDataMaxSizeInBytes + 255) & ~256;
}

void DX12Raytracing::GenerateBottomLevelAS(ID3D12GraphicsCommandList4* commandList, DX12Object* storedObject, bool updateOnly) {
	assert(updateOnly, "Updating a BLAS is currently not supported.");

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