#pragma hdrstop

#include "./dx12_AccelerationStructure.h"

namespace DX12Rendering {
#pragma region TopLevelAccelerationStructure
	BottomLevelAccelerationStructure::BottomLevelAccelerationStructure() 
	{}

	BottomLevelAccelerationStructure::~BottomLevelAccelerationStructure()
	{}

	DX12AccelerationObject* BottomLevelAccelerationStructure::AddAccelerationObject(const dxObjectIndex_t& key, DX12VertexBuffer& vertexBuffer, UINT vertexOffset, DX12IndexBuffer& indexBuffer, UINT indexOffset, UINT indexCount)
	{
		if (GetAccelerationObject(key) != nullptr) 
		{
			FailMessage(L"BottomLevelAccelerationStructure::AddAccelerationObject failed due to an already existing acceleration object.");
			return nullptr;
		}

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.vertexBuffer->GetGPUVirtualAddress() + vertexOffset;
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer.vertexBufferView.StrideInBytes;
		geometryDesc.Triangles.VertexCount = vertexBuffer.vertexBufferView.SizeInBytes / vertexBuffer.vertexBufferView.StrideInBytes;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		geometryDesc.Triangles.IndexBuffer = indexBuffer.indexBuffer->GetGPUVirtualAddress() + indexOffset;
		geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometryDesc.Triangles.IndexCount = indexCount;
		geometryDesc.Triangles.Transform3x4 = NULL; //TODO: Check if we need to add a transform here.
		geometryDesc.Flags = true ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
			: D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; //TODO: Eventually add support for opaque geometry.

		m_vertexBuffers.emplace_back(geometryDesc);
		m_objectMap.emplace(key, m_vertexBuffers.back());
	}

	DX12AccelerationObject* BottomLevelAccelerationStructure::GetAccelerationObject(const dxObjectIndex_t& key) {
		auto result = m_objectMap.find(key);

		if (result != m_objectMap.end()) {
			return &result->second;
		}

		return nullptr;
	}

	void BottomLevelAccelerationStructure::Reset()
	{
		m_objectMap.clear();
		m_vertexBuffers.clear();
		m_result = nullptr;
	}

	void BottomLevelAccelerationStructure::Generate(ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, UINT64 inputScratchSizeInBytes)
	{
		bool isUpdate = m_result.Get() != nullptr;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = isUpdate ? 
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE :
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		UINT64 testScratchSizeInBytes;
		UINT64 resultSizeInBytes;

		CalculateBufferSize(device, &testScratchSizeInBytes, &resultSizeInBytes, flags);

		if (resultSizeInBytes > m_resultSizeInBytes)
		{
			// Create the result buffer
			D3D12_RESOURCE_DESC description = {};
			description.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			description.DepthOrArraySize = 1;
			description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			description.Format = DXGI_FORMAT_UNKNOWN;
			description.Height = 1;
			description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			description.MipLevels = 1;
			description.SampleDesc.Count = 1;
			description.SampleDesc.Quality = 0;
			description.Width = resultSizeInBytes;

			D3D12_HEAP_PROPERTIES heapProps = {
				D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0
			};

			ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&m_result)));
			
			m_resultSizeInBytes = resultSizeInBytes;
			isUpdate = false; // Since we built a new resource, all objects will be updated inside.
		}

		assert(inputScratchSizeInBytes >= testScratchSizeInBytes);

		// Describe the BLAS data
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc;
		blasDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		blasDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		blasDesc.Inputs.NumDescs = m_vertexBuffers.size();
		blasDesc.Inputs.pGeometryDescs = m_vertexBuffers.data();
		blasDesc.Inputs.Flags = flags;
		blasDesc.DestAccelerationStructureData = { m_result->GetGPUVirtualAddress() };
		blasDesc.ScratchAccelerationStructureData = { scratchBuffer->GetGPUVirtualAddress() };
		blasDesc.SourceAccelerationStructureData = isUpdate ? m_result->GetGPUVirtualAddress() : NULL;

		// Build the acceleration structure.
		commandList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

		// TODO: Possibly remove this.
		// Wait for the building of the blas to complete.
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = m_result.Get();
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		commandList->ResourceBarrier(1, &uavBarrier);
	}

	void BottomLevelAccelerationStructure::CalculateBufferSize(
		ID3D12Device5* device, 
		UINT64* scratchSizeInBytes, 
		UINT64* resultSizeInBytes, 
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS desc = {};
		desc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		desc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		desc.NumDescs = m_vertexBuffers.size();
		desc.pGeometryDescs = m_vertexBuffers.data();
		desc.Flags = flags;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};

		device->GetRaytracingAccelerationStructurePrebuildInfo(&desc, &info);

		*scratchSizeInBytes = DX12_ALIGN(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		*resultSizeInBytes = DX12_ALIGN(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	}
#pragma endregion

#pragma region TopLevelAccelerationStructure
	TopLevelAccelerationStructure::TopLevelAccelerationStructure(ID3D12Device5* device)
		: m_device(device)
	{
		UINT64 scratchSizeInBytes;
		UINT64 resultSizeInBytes;
		UINT64 instanceDescsSize;

		CacluateBufferSizes(&scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);
		assert(scratchSizeInBytes < DEFAULT_SCRATCH_SIZE, "The generated objects scratch size is too large.");

		if (m_result == nullptr) {
			m_result = CreateBuffer(m_device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
			m_result->SetName(L"TLAS Result Buffer");
		}
	}

	TopLevelAccelerationStructure::~TopLevelAccelerationStructure() {

	}

	void TopLevelAccelerationStructure::Reset() {
		m_instances.clear();
	}

	void TopLevelAccelerationStructure::AddInstance(const DX12Object* storedObject, const DX12Stage* stage) {
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

		if (m_instanceDesc == nullptr) {
			// TODO: Check if our descriptor size is too small.
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
#pragma endregion
}