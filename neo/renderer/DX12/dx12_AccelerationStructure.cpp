#pragma hdrstop

#include "./dx12_AccelerationStructure.h"

namespace DX12Rendering {
#pragma region TopLevelAccelerationStructure
	BottomLevelAccelerationStructure::BottomLevelAccelerationStructure() 
	{}

	BottomLevelAccelerationStructure::~BottomLevelAccelerationStructure()
	{}

	DX12AccelerationObject* BottomLevelAccelerationStructure::AddAccelerationObject(const dxHandle_t& key, DX12VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount)
	{
		if (GetAccelerationObject(key) != nullptr) 
		{
			FailMessage("BottomLevelAccelerationStructure::AddAccelerationObject failed due to an already existing acceleration object.");
			return nullptr;
		}

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer->vertexBuffer->GetGPUVirtualAddress() + vertexOffsetBytes;
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer->vertexBufferView.StrideInBytes;
		geometryDesc.Triangles.VertexCount = vertexCount;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		geometryDesc.Triangles.IndexBuffer = indexBuffer->indexBuffer->GetGPUVirtualAddress() + indexOffset;
		geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometryDesc.Triangles.IndexCount = indexCount;
		geometryDesc.Triangles.Transform3x4 = NULL; //TODO: Check if we need to add a transform here.
		geometryDesc.Flags = true ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
			: D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; //TODO: Eventually add support for opaque geometry.

		m_vertexBuffers.emplace_back(geometryDesc);
		m_objectMap.try_emplace(key, &m_vertexBuffers.back(), static_cast<UINT>(m_vertexBuffers.size() - 1));

		m_isDirty = true;
	}

	DX12AccelerationObject* BottomLevelAccelerationStructure::GetAccelerationObject(const dxHandle_t& key) 
	{
		auto result = m_objectMap.find(key);

		if (result != m_objectMap.end()) 
		{
			return &result->second;
		}

		return nullptr;
	}

	void BottomLevelAccelerationStructure::RemoveAccelerationObject(const dxHandle_t& key) 
	{
		DX12AccelerationObject* result = GetAccelerationObject(key);

		if (result != nullptr)
		{
			m_objectMap.erase(key);
		}
	}

	void BottomLevelAccelerationStructure::Reset()
	{
		m_objectMap.clear();
		m_vertexBuffers.clear();
		m_result = nullptr;
		m_isDirty = true;
	}

	void BottomLevelAccelerationStructure::Generate(ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, UINT64 inputScratchSizeInBytes, LPCWSTR name)
	{
		static UINT count = 0;
		const UINT countMax = 1; // NOTE: TEMP TO DELAY BLAS CONSTRUCTION
		if (!m_isDirty && count > countMax) {
			return;
		}
		else if (count < countMax) {
			++count;
			return;
		}

		m_isDirty = false;
		bool isUpdate = m_result.Get() != nullptr;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = isUpdate ?
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE :
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE; // For now we will always allow updates in our BLAS

		UINT64 testScratchSizeInBytes;
		UINT64 resultSizeInBytes;


		CalculateBufferSize(device, &testScratchSizeInBytes, &resultSizeInBytes, flags);

		if (resultSizeInBytes > m_defaultResultSizeInBytes)
		{
			FailMessage("BottomLevelAccelerationStructure: Invalid calculated buffer size.");
			return;
		}

		if (!isUpdate)
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
			description.Width = m_defaultResultSizeInBytes;

			ThrowIfFailed(device->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&m_result)));
			
			m_resultSizeInBytes = resultSizeInBytes;
			isUpdate = false; // Since we built a new resource, all objects will be updated inside.

			m_result->SetName(name);
		} 
		else
		{
			// Provide a barrier to make sure we are done using the resource.
			D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_result.Get());
			commandList->ResourceBarrier(1, &uavBarrier);
		}

		assert(inputScratchSizeInBytes >= testScratchSizeInBytes);

		// Describe the BLAS data
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc;
		blasDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		blasDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		blasDesc.Inputs.NumDescs = static_cast<UINT>(m_vertexBuffers.size());
		blasDesc.Inputs.pGeometryDescs = m_vertexBuffers.data();
		blasDesc.Inputs.Flags = flags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
		blasDesc.DestAccelerationStructureData = { m_result->GetGPUVirtualAddress() };
		blasDesc.ScratchAccelerationStructureData = { scratchBuffer->GetGPUVirtualAddress() };
		blasDesc.SourceAccelerationStructureData = isUpdate ? m_result->GetGPUVirtualAddress() : NULL;

		// Build the acceleration structure.
		commandList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

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

#ifdef DEBUG_IMGUI
	const void BottomLevelAccelerationStructure::ImGuiDebug()
	{
		if (m_result == nullptr)
		{
			ImGui::TextColored(ImGui_Color_Error, "Result - None");
		}
		else
		{
			ImGui::Text("Result GPU Addr: 0x%08x", m_result->GetGPUVirtualAddress());
			ImGui::Text("Total Elements: %d", m_objectMap.size());
		}
	}
#endif
#pragma endregion

#pragma region TopLevelAccelerationStructure
	TopLevelAccelerationStructure::TopLevelAccelerationStructure(BottomLevelAccelerationStructure* blas)
		: m_blas(blas)
	{
		for (UINT i = 0; i < DX12_FRAME_COUNT; ++i)
		{
			m_instances[i].reserve(512); //TODO: Setup an object to make this the max size.
		}
	}

	TopLevelAccelerationStructure::~TopLevelAccelerationStructure() 
	{
		Reset();

		m_blas = nullptr;

		if (m_result != nullptr)
		{
			m_result = nullptr;
		}

		if (m_instanceDesc != nullptr)
		{
			m_instanceDesc = nullptr;
		}
	}

	void TopLevelAccelerationStructure::IncrementFrameIndex()
	{
		m_readFrameIndex = m_writeFrameIndex;
		++m_writeFrameIndex;

		if (m_writeFrameIndex >= DX12_FRAME_COUNT)
		{
			m_writeFrameIndex = 0;
		}
	}

	void TopLevelAccelerationStructure::Reset() 
	{
		m_readFrameIndex = 0;
		m_writeFrameIndex = 1;
		
		m_instances[m_readFrameIndex].clear();
		m_instances[m_writeFrameIndex].clear();
	}

	const bool TopLevelAccelerationStructure::TryGetWriteInstance(const UINT& index, DX12Rendering::Instance **outInstance) 
	{
		for (auto instance : m_instances[m_writeFrameIndex]) {
			if (instance.instanceId == index)
			{
				*outInstance = &instance;
				return true;
			}
		}

		return false;
	}

	void TopLevelAccelerationStructure::AddInstance(const dxHandle_t& index, const float transform[16]) {
		auto* object = m_blas->GetAccelerationObject(index);

		if (object == nullptr)
		{
			assert("Tried adding an invalid object handle to TLAS.");
			return;
		}

		DX12Rendering::Instance *instance;
		if (TryGetWriteInstance(object->index, &instance))
		{
			memcpy(instance->transformation, transform, sizeof(float[16]));
			return;
		}

		m_instances[m_writeFrameIndex].emplace_back(transform, object->index, 0 /* TODO: Find the hit group index containing the normal map of the surface. */);
	}

	void TopLevelAccelerationStructure::FillInstanceDescriptor(ID3D12Device5* device, UINT64 instanceDescsSize)
	{
		auto instances = m_instances[m_readFrameIndex];
		bool shouldCleanMemory = m_lastInstanceCount > instances.size();

		if (m_instanceDesc == nullptr || m_instanceDescsSize < instanceDescsSize) {
			m_instanceDescsSize = instanceDescsSize;
			m_instanceDesc = CreateBuffer(device, instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
			shouldCleanMemory = true;
		}

		D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
		m_instanceDesc->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs));
		if (!instanceDescs)
		{
			FailMessage("TopLevelAccelerationStructure error: Cannot map instance descriptors.");
		}

		if (shouldCleanMemory)
		{
			// Make sure we're dealing with clean memory.
			ZeroMemory(instanceDescs, m_instanceDescsSize);
		}

		m_lastInstanceCount = static_cast<UINT>(instances.size());
		for (UINT32 index = 0; index < m_lastInstanceCount; ++index)
		{
			instanceDescs[index].InstanceID = instances[index].instanceId;
			instanceDescs[index].InstanceContributionToHitGroupIndex = instances[index].hitGroupIndex;
			instanceDescs[index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE; // Should we implement back face culling?
			
			// Model View Matrix matrix
			memcpy(instanceDescs[index].Transform, &instances[index].transformation, sizeof(instanceDescs[index].Transform));

			instanceDescs[index].AccelerationStructure = m_blas->GetGPUVirtualAddress();
			
			// Always visible.
			instanceDescs[index].InstanceMask = 0xFF;
		}

		m_instanceDesc->Unmap(0, nullptr);
	}

	bool TopLevelAccelerationStructure::UpdateResources(ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, UINT scratchBufferSize) {
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = false
			? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
			: D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		UINT64 scratchSizeInBytes;
		UINT64 resultSizeInBytes;
		UINT64 instanceDescsSize;
		
		if (m_instances[m_readFrameIndex].size() == 0) {
			return false;
		}

		CacluateBufferSizes(device, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize);
		assert(scratchSizeInBytes < scratchBufferSize);
		
		if (instanceDescsSize == 0) {
			return false;
		}

		FillInstanceDescriptor(device, instanceDescsSize);

		if (resultSizeInBytes > m_resultSize)
		{
			if (m_result != nullptr)
			{
				m_result = nullptr;
			}

			m_result = CreateBuffer(device, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
			m_resultSize = resultSizeInBytes;
		}

		D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = 0;// updateOnly ? previousResult->GetGPUVirtualAddress() : 0;
		
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC descriptor = {};
		descriptor.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		descriptor.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		descriptor.Inputs.InstanceDescs = m_instanceDesc->GetGPUVirtualAddress();
		descriptor.Inputs.NumDescs = static_cast<UINT>(m_instances[m_readFrameIndex].size());
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

		return true;
	}

	void TopLevelAccelerationStructure::CacluateBufferSizes(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize) { 
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
		device->GetRaytracingAccelerationStructurePrebuildInfo(&description, &info);

		// 256 Align the storage size.
		*scratchSizeInBytes = DX12_ALIGN(info.ScratchDataSizeInBytes, 256);
		*resultSizeInBytes = DX12_ALIGN(info.ResultDataMaxSizeInBytes, 256);

		*instanceDescsSize = DX12_ALIGN(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(m_instances[m_readFrameIndex].size()), 256);
	}


#ifdef DEBUG_IMGUI
	const void TopLevelAccelerationStructure::ImGuiDebug()
	{
		// Result
		if (m_result == nullptr)
		{
			ImGui::TextColored(ImGui_Color_Error, "Result - None");
		}
		else
		{
			ImGui::Text("Result GPU Addr: 0x%08x", m_result->GetGPUVirtualAddress());
		}

		ImGui::Text("Result Size (In Bytes): %d", m_resultSize);

		// Instances
		if (m_instanceDesc == nullptr)
		{
			ImGui::TextColored(ImGui_Color_Error, "Instance: None");
		}
		else
		{
			ImGui::Text("Instance GPU Addr: 0x%08x", m_instanceDesc->GetGPUVirtualAddress());
		}

		ImGui::Text("Instance Size (In Bytes): %d", m_instanceDescsSize);
		ImGui::Text("Instance Count: %d, Last Instance Count: %d", m_instances[m_readFrameIndex].size(), m_lastInstanceCount);
	}
#endif
#pragma endregion
}