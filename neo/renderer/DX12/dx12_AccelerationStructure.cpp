#pragma hdrstop

#include "./dx12_AccelerationStructure.h"
#include "./dx12_DeviceManager.h"
#include "./dx12_CommandList.h"

#include "./dx12_shader.h"

#include <algorithm>

namespace
{
	bool CopyInstanceToDescriptor(const DX12Rendering::Instance& instance, D3D12_RAYTRACING_INSTANCE_DESC& desc, DX12Rendering::BLASManager& blasManager)
	{
		bool result = true;

		desc.InstanceID = instance.instanceId;
		desc.InstanceContributionToHitGroupIndex = instance.hitGroupIndex;
		desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;// | D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE; // Should we implement back face culling?

		// Model View Matrix matrix
		memcpy(desc.Transform, &instance.transformation, sizeof(desc.Transform));

		const auto blas = blasManager.GetBLAS(instance.blasId);
		if (blas == nullptr || blas->state != DX12Rendering::Resource::eResourceState::Ready)
		{
			result = false;
			desc.AccelerationStructure = 0;

			// Unreachable
			desc.InstanceMask = DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_NONE;
		}
		else
		{
			desc.AccelerationStructure = blas->resource->GetGPUVirtualAddress();

			// Always visible.
			desc.InstanceMask = instance.mask; //TODO: setup so we track shadow casting instances. Eventually we can add surfaces with emmisive as well.
		}

		return result;
	}

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> BuildInstanceDescriptors(DX12Rendering::BLASManager& blasManager, const std::vector<DX12Rendering::Instance>& instances)
	{
		const UINT instanceCount = instances.size();
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descriptors;
		descriptors.reserve(instanceCount);

		UINT descIndex = 0;
		for(const DX12Rendering::Instance& instance : instances)
		{
			D3D12_RAYTRACING_INSTANCE_DESC desc;
			if (CopyInstanceToDescriptor(instance, desc, blasManager))
			{
				descriptors.push_back(desc);
				++descIndex;
			}
		};

		return descriptors;
	}
}

namespace DX12Rendering {
#pragma region BottomLevelAccelerationStructure
	void BottomLevelAccelerationStructure::CalculateBufferSize(
		ID3D12Device5* device,
		UINT64* scratchSizeInBytes,
		UINT64* resultSizeInBytes,
		const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* desc)
	{
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};

		device->GetRaytracingAccelerationStructurePrebuildInfo(desc, &info);

		*scratchSizeInBytes = DX12_ALIGN(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		*resultSizeInBytes = DX12_ALIGN(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	}

	void BottomLevelAccelerationStructure::AddGeometry(DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount, dxHandle_t jointsHandle)
	{
		auto vertexDesc = vertexBuffer->GetView();
		auto indexDesc = indexBuffer->GetView();

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.VertexBuffer.StartAddress = vertexDesc->BufferLocation + vertexOffsetBytes;
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexDesc->StrideInBytes;
		geometryDesc.Triangles.VertexCount = vertexCount;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		geometryDesc.Triangles.IndexBuffer = indexDesc->BufferLocation + indexOffset;
		geometryDesc.Triangles.IndexFormat = indexDesc->Format;
		geometryDesc.Triangles.IndexCount = indexCount;
		geometryDesc.Triangles.Transform3x4 = NULL; //TODO: Check if we need to add a transform here.
		geometryDesc.Flags = true ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
			: D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; //TODO: Eventually add support for opaque geometry.

		// Only dynamic objects can have joints
		if (!m_isStatic)
		{
			joints.emplace_back(jointsHandle);
		}

		AddGeometry(geometryDesc);
	}

	void BottomLevelAccelerationStructure::AddGeometry(D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc)
	{
		geometry.emplace_back(geometryDesc);

		if (state > Unallocated && state < Removed)
		{
			state = Dirty;
		}
	}

	bool BottomLevelAccelerationStructure::Generate(ScratchBuffer& scratch)
	{
		//TODO: Move the BLAS resource into BLASManager and control our own alocation.

		if (state == Ready || !isBuilt) {
			return false; // Nothing to update
		}

		ID3D12Device5* device = Device::GetDevice();

		if (device == nullptr || !fence.IsFenceCompleted())
		{
			return false; // We are not ready to generate.
		}

		bool isUpdate = state == Dirty;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		
		if (m_isStatic)
		{
			flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		}
		else
		{
			flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
			flags |= isUpdate ?
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE :
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE; // For now we will always allow updates in our BLAS
		}

		DX12Rendering::Commands::CommandList* commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE)->RequestNewCommandList();
		DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "BottomLevelAccelerationStructure::Generate");

		UINT64 requestedScratchSize;
		UINT64 scratchLocation;
		UINT64 resultSizeInBytes;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputDesc = {};
		inputDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		inputDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputDesc.NumDescs = geometry.size();
		inputDesc.pGeometryDescs = geometry.data();
		inputDesc.Flags = flags;

		CalculateBufferSize(device, &requestedScratchSize, &resultSizeInBytes, &inputDesc);

		if (!scratch.RequestSpace(commandList, requestedScratchSize, scratchLocation))
		{
			// The scratch is full and we cannot build at the moment.
			return false;
		}

		if (isUpdate)
		{
			// Provide a barrier to make sure we are done using the resource.
			D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.Get());
			commandList->CommandResourceBarrier(1, &uavBarrier);
		}
		else
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

			if (!Allocate(description, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps))
			{
				return false; // Failed to create resource.
			}

			m_sizeInBytes = resultSizeInBytes;
		}

		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			// Describe the BLAS data
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc = {};
			blasDesc.Inputs = inputDesc;
			blasDesc.DestAccelerationStructureData = resource->GetGPUVirtualAddress();
			blasDesc.ScratchAccelerationStructureData = scratch.resource->GetGPUVirtualAddress() + scratchLocation; // Point to the space in the scratch to build from.
			blasDesc.SourceAccelerationStructureData = isUpdate ? resource->GetGPUVirtualAddress() : NULL;
			
			// Build the acceleration structure.
			commandList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

			// Wait for the building of the blas to complete.
			D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.Get());
			commandList->ResourceBarrier(1, &uavBarrier);
		});

		commandList->AddPostFenceSignal(&fence); // Add our fence that we will signal on exit.
	}

#pragma endregion
#pragma region BLASContainer
	BLASManager::BLASManager() :
		m_scratchBuffer(DEFAULT_BLAS_SCRATCH_SIZE, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, kDefaultHeapProps, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"BLAS Scratch Buffer")
	{
	}

	BLASManager::~BLASManager()
	{
		Reset();
	}

	BottomLevelAccelerationStructure* BLASManager::CreateBLAS(const dxHandle_t& key, const bool isStatic, const bool isBuilt, const LPCWSTR name)
	{
		BottomLevelAccelerationStructure* blas = GetBLAS(key);
		if (blas)
		{
			blas->isBuilt = isBuilt;
			return blas;
		}

		blas = &m_objectMap.emplace(key, BottomLevelAccelerationStructure(key, isStatic, name)).first->second;
		blas->isBuilt = isBuilt;

		return blas;
	}

	BottomLevelAccelerationStructure* BLASManager::GetBLAS(const dxHandle_t& key)
	{
		auto result = m_objectMap.find(key);

		if (result != m_objectMap.end())
		{
			return &result->second;
		}

		return nullptr;
	}

	void BLASManager::RemoveBLAS(const dxHandle_t& key)
	{
		BottomLevelAccelerationStructure* result = GetBLAS(key);

		if (result != nullptr)
		{
			m_objectMap.erase(key);
		}
	}

	void BLASManager::Reset()
	{
		m_blasIndex = 0;

		m_objectMap.clear();
	}

	UINT BLASManager::Generate()
	{
		// TODO: Define starting point for BLAS using m_blasIndex
		auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE);
		DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "BLASManager::Generate");

		UINT count = 0;
		UINT readCount = 0;
		for (auto blasPair = m_objectMap.begin(); blasPair != m_objectMap.end(); ++blasPair)
		{
			++readCount;

			if (blasPair->second.Generate(m_scratchBuffer))
			{
				++count;

				if (count >= m_blasPerFrame)
				{
					break;
				}
			}
			else if (!m_scratchBuffer.fence.IsFenceCompleted())//!blasPair->second.Exists() && m_scratchBuffer.state == DX12Rendering::Resource::Dirty)
			{
				// Our scratch buffer is full, so we should wait.
				break;
			}
		}

		return count;
	}

#ifdef DEBUG_IMGUI
	const void BLASManager::ImGuiDebug()
	{
		if (m_objectMap.size() == 0)
		{
			ImGui::TextColored(ImGui_Color_Error, "Result - None");
		}
		else
		{
			ImGui::Text("Total Elements: %d", m_objectMap.size());
		}
	}
#endif
#pragma endregion

#pragma region InstanceDescriptor
	UINT InstanceDescriptor::Fill(UINT64 instanceDescsSize, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescriptors)
	{
		const UINT instanceCount = instanceDescriptors.size();

		bool shouldCleanMemory = m_lastInstanceCount > instanceCount;

		if (state != Ready || m_instanceDescsSize < instanceDescsSize) {
			Release();

			D3D12_RESOURCE_DESC description = {};
			description.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			description.DepthOrArraySize = 1;
			description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			description.Flags = D3D12_RESOURCE_FLAG_NONE;
			description.Format = DXGI_FORMAT_UNKNOWN;
			description.Height = 1;
			description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			description.MipLevels = 1;
			description.SampleDesc.Count = 1;
			description.SampleDesc.Quality = 0;
			description.Width = instanceDescsSize;

			m_instanceDescsSize = instanceDescsSize;
			Allocate(description, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);

			shouldCleanMemory = true;
		}
		else
		{
			shouldCleanMemory = true;
		}

		D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
		resource->Map(0, nullptr, (void**)(&instanceDescs));
		if (!instanceDescs)
		{
			FailMessage("TopLevelAccelerationStructure error: Cannot map instance descriptors.");
		}

		if (shouldCleanMemory)
		{
			// Make sure we're dealing with clean memory.
			ZeroMemory(instanceDescs, m_instanceDescsSize);
		}

		m_lastInstanceCount = static_cast<UINT>(instanceCount);
		UINT descIndex = 0;
		for(const D3D12_RAYTRACING_INSTANCE_DESC& instance : instanceDescriptors)
		{
			memcpy(&instanceDescs[descIndex], &instance, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
			++descIndex;
		};

		resource->Unmap(0, nullptr);

		return descIndex;
	}

#ifdef DEBUG_IMGUI
	const void InstanceDescriptor::ImGuiDebug()
	{
		// Each TLAS
		if (state == Unallocated || state == Removed)
		{
			ImGui::TextColored(ImGui_Color_Error, "Instance: None");
		}
		else
		{
			ImGui::Text("Instance GPU Addr: 0x%08x", resource->GetGPUVirtualAddress());
		}

		ImGui::Text("Instance Size (In Bytes): %d", m_instanceDescsSize);
	}
#endif
#pragma endregion

#pragma region TopLevelAccelerationStructure
	TopLevelAccelerationStructure::~TopLevelAccelerationStructure()
	{
	}

	bool TopLevelAccelerationStructure::UpdateResources(BLASManager& blasManager, const std::vector<DX12Rendering::Instance>& instances, ScratchBuffer* scratchBuffer) {
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = false
			? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
			: D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		UINT64 scratchSizeInBytes;
		UINT64 resultSizeInBytes;
		UINT64 instanceDescsSize;

		//TODO: build an array of descriptors first. That way we do not need to rely on the static instnces after. We'll then copy the results after.

		const UINT instanceCount = instances.size();

		if (instanceCount == 0)
		{
			return true;
		}

		auto device = DX12Rendering::Device::GetDevice();
		if (device == nullptr)
		{
			return false;
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputDesc = {};
		inputDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		inputDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputDesc.NumDescs = instanceCount;
		inputDesc.Flags = flags;
		inputDesc.InstanceDescs = NULL;

		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDesc = BuildInstanceDescriptors(blasManager, instances);
		inputDesc.NumDescs = instanceDesc.size();

		CacluateBufferSizes(device, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize, inputDesc.NumDescs, &inputDesc);

		if (instanceDescsSize == 0) {
			return false;
		}

		assert(scratchSizeInBytes <= DEFAULT_TLAS_SCRATCH_SIZE);

		// Rebuild the resource each time, as this was reccomended by NVIDIA as the best TLAS practice
		if(resultSizeInBytes != m_resultSize)
		{
			Release();
			
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

			Allocate(description, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
			m_resultSize = resultSizeInBytes;
		}

		// Create the descriptor information for the tlas data
		inputDesc.NumDescs = m_instanceDescriptor.Fill(instanceDescsSize, instanceDesc);
		inputDesc.InstanceDescs = m_instanceDescriptor.resource->GetGPUVirtualAddress();

		auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE)->RequestNewCommandList();
		DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "TopLevelAccelerationStructure::UpdateResources");
		
		UINT64 scratchLocation;
		bool spaceResult = scratchBuffer->RequestSpace(commandList, scratchSizeInBytes, scratchLocation, false);
		
		ID3D12Resource* resource = this->resource.Get();

		// Build the top level AS
		commandList->AddCommandAction([resource, scratchLocation, inputDesc, scratchBuffer](ID3D12GraphicsCommandList4* commandList)
		{
			D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = NULL;// updateOnly ? previousResult->GetGPUVirtualAddress() : 0;

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC descriptor = {};
			descriptor.Inputs = inputDesc;

			descriptor.DestAccelerationStructureData = resource->GetGPUVirtualAddress();
			descriptor.ScratchAccelerationStructureData = scratchBuffer->resource->GetGPUVirtualAddress() + scratchLocation;
			descriptor.SourceAccelerationStructureData = pSourceAS;

			commandList->BuildRaytracingAccelerationStructure(&descriptor, 0, nullptr);

			// Wait for the buffer to complete setup.
			D3D12_RESOURCE_BARRIER uavBarrier;
			uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uavBarrier.UAV.pResource = resource;
			uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			commandList->ResourceBarrier(1, &uavBarrier);
		});

		commandList->AddPostFenceSignal(&fence);

		return true;
	}

	void TopLevelAccelerationStructure::CacluateBufferSizes(
		ID3D12Device5* device, 
		UINT64* scratchSizeInBytes, 
		UINT64* resultSizeInBytes, 
		UINT64* instanceDescsSize, 
		const UINT instanceCount, 
		const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* description) 
	{
		// Calculate the space needed to generate the Acceleration Structure
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		device->GetRaytracingAccelerationStructurePrebuildInfo(description, &info);

		// 256 Align the storage size.
		*scratchSizeInBytes = DX12_ALIGN(info.ScratchDataSizeInBytes, 256);
		*resultSizeInBytes = DX12_ALIGN(info.ResultDataMaxSizeInBytes, 256);

		*instanceDescsSize = DX12_ALIGN(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(instanceCount), 256);
	}

#ifdef DEBUG_IMGUI
	const void TopLevelAccelerationStructure::ImGuiDebug()
	{
		// Each TLAS
		if (state == Unallocated || state == Removed)
		{
			ImGui::TextColored(ImGui_Color_Error, "Result - None");
		}
		else
		{
			ImGui::Text("Result GPU Addr: 0x%08x", resource->GetGPUVirtualAddress());
		}

		ImGui::Text("Result Size (In Bytes): %d", m_resultSize);

		// Instance Descriptor
		m_instanceDescriptor.ImGuiDebug();
	}
#endif
#pragma endregion

#pragma region TLASManager
	TLASManager::TLASManager(BLASManager* blasManager) :
		m_accelerationCooldown(255),
		m_blasManager(blasManager),
		m_isDirty(false),
		m_scratch(DEFAULT_TLAS_SCRATCH_SIZE, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, kDefaultHeapProps, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"TLAS Scratch Buffer"),
		m_tlas{ TopLevelAccelerationStructure(L"TLAS_0"), TopLevelAccelerationStructure(L"TLAS_1") }
	{
		Reset();
	}

	TLASManager::~TLASManager()
	{

	}

	void TLASManager::Reset()
	{
		DX12Rendering::WriteLock instanceLock(m_instanceLock);

		m_isDirty = false;

		const UINT8 frameIndex = GetCurrentFrameIndex();

		m_instances[frameIndex].clear();
		m_instances[frameIndex].reserve(DEFAULT_TLAS_COUNT);
	}

	void TLASManager::UpdateDynamicInstances()
	{
		const UINT frameIndex = GetCurrentFrameIndex();

		for (Instance& instance : m_instances[frameIndex])
		{
			// TODO
		}
	}

	bool TLASManager::Generate()
	{
		bool result = false;
		{
			DX12Rendering::WriteLock instanceLock(m_instanceLock);

			auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE);
			DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "TLASManager::Generate");

			if (m_scratch.state != Resource::Ready)
			{
				m_scratch.Build();
			}

			UINT instanceCount = m_instances[GetCurrentFrameIndex()].size();

			result = GetCurrent().UpdateResources(*m_blasManager, m_instances[GetCurrentFrameIndex()], &m_scratch);

			assert(instanceCount == m_instances[GetCurrentFrameIndex()].size()); // Verify that no new instances have been added during execution.

			if (result)
			{
				m_isDirty = false;
				
				Reset();
			}
		}	

		return result;
	}

	void TLASManager::AddInstance(const dxHandle_t& entityId, const dxHandle_t& blasId, const float transform[16], const ACCELERATION_INSTANCE_TYPE instanceTypes, ACCELLERATION_INSTANCE_MASK instanceMask)
	{
		const BottomLevelAccelerationStructure* blas = m_blasManager->GetBLAS(blasId);
		if (blas == nullptr)
		{
			return;
		}

		{
			DX12Rendering::WriteLock instanceLock(m_instanceLock);

			const UINT frameIndex = GetNextFrameIndex();

			MarkDirty();

			DX12Rendering::Instance* instance;
			if (TryGetWriteInstance(frameIndex, entityId, instanceTypes, &instance))
			{
				instance->blasId = blasId;
				instance->mask = instanceMask;

				memcpy(instance->transformation, transform, sizeof(float[3][4]));
				return;
			}

			UINT hitShaderIndex = 0; /* TODO: Find the hit group index containing the normal map of the surface. */
			m_instances[frameIndex].emplace_back(transform, entityId, blas->id, hitShaderIndex, instanceMask, instanceTypes);
		}
	}

	const bool TLASManager::TryGetWriteInstance(const UINT frameIndex, const dxHandle_t& index, const ACCELERATION_INSTANCE_TYPE typesMask, DX12Rendering::Instance** outInstance)
	{
		for (DX12Rendering::Instance& instance : m_instances[frameIndex])
		{
			if (instance.instanceId == index)
			{
				*outInstance = &instance;
				return true;
			}
		}

		return false;
	}

#ifdef DEBUG_IMGUI
	const void TLASManager::ImGuiDebug()
	{
		ImGui::Text("Instance Count: %d", m_instances[GetCurrentFrameIndex()].size());

		// Each TLAS
		TopLevelAccelerationStructure& tlas = GetCurrent();
		
		char title[50];
		std::sprintf(title, "%ls", tlas.GetName());

		if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
		{
			tlas.ImGuiDebug();
		}
	}
#endif
#pragma endregion
}