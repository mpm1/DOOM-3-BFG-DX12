#pragma hdrstop

#include "./dx12_AccelerationStructure.h"
#include "./dx12_DeviceManager.h"
#include "./dx12_CommandList.h"

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

	void BottomLevelAccelerationStructure::AddGeometry(DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount)
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

		if (state == Ready) {
			return true; // Nothing to update
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

		DX12Rendering::Commands::CommandList* commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COMPUTE);

		if (!scratch.RequestSpace(commandList, requestedScratchSize, scratchLocation))
		{
			// The scratch is full and we cannot build at the moment.
			return false;
		}

		if (isUpdate)
		{
			commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
			{
				// Provide a barrier to make sure we are done using the resource.
				D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.Get());
				commandList->ResourceBarrier(1, &uavBarrier);
			});
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

		commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
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

			fence.Signal(device, commandQueue);
		});
	}

#pragma endregion
#pragma region BLASContainer
	BLASManager::BLASManager() :
		m_scratchBuffer(DEFAULT_BLAS_SCRATCH_SIZE, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, L"BLAS Scratch Buffer")
	{}

	BLASManager::~BLASManager()
	{
		Reset();
	}

	BottomLevelAccelerationStructure* BLASManager::CreateBLAS(const dxHandle_t& key, const LPCWSTR name)
	{
		BottomLevelAccelerationStructure* blas = GetBLAS(key);
		if (blas)
		{
			return blas;
		}

		blas = &m_objectMap.emplace(key, BottomLevelAccelerationStructure(key, true /* All blas are static for now */, name)).first->second;

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

		UINT count = 0;
		for (auto blasPair = m_objectMap.begin(); blasPair != m_objectMap.end(); ++blasPair)
		{
			if (blasPair->second.Generate(m_scratchBuffer))
			{
				++count;

				if (count >= m_blasPerFrame)
				{
					break;
				}
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
	void InstanceDescriptor::Fill(BLASManager& blasManager, UINT64 instanceDescsSize, const DX12Rendering::Instance* instances, const UINT instanceCount)
	{
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

		D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
		resource->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs));
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
		for (UINT32 index = 0; index < m_lastInstanceCount; ++index)
		{
			instanceDescs[index].InstanceID = instances[index].instanceId;
			instanceDescs[index].InstanceContributionToHitGroupIndex = instances[index].hitGroupIndex;
			instanceDescs[index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE; // Should we implement back face culling?

			// Model View Matrix matrix
			memcpy(instanceDescs[index].Transform, &instances[index].transformation, sizeof(instanceDescs[index].Transform));

			auto blas = blasManager.GetBLAS(instances[index].instanceId);
			if (blas == nullptr || blas->state != Ready)
			{
				instanceDescs[index].AccelerationStructure = 0;

				// Unreachable
				instanceDescs[index].InstanceMask = 0x00;
			}
			else
			{
				instanceDescs[index].AccelerationStructure = blas->resource->GetGPUVirtualAddress();

				// Always visible.
				instanceDescs[index].InstanceMask = 0xFF;
			}
		}

		resource->Unmap(0, nullptr);
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

	bool TopLevelAccelerationStructure::UpdateResources(BLASManager& blasManager, const DX12Rendering::Instance* instances, const UINT instanceCount, ScratchBuffer* scratchBuffer) {
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = false
			? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
			: D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		UINT64 scratchSizeInBytes;
		UINT64 resultSizeInBytes;
		UINT64 instanceDescsSize;

		if (instanceCount == 0 || !fence.IsFenceCompleted())
		{
			return false;
		}

		auto device = DX12Rendering::Device::GetDevice();
		if (device == nullptr)
		{
			return false;
		}

		CacluateBufferSizes(device, &scratchSizeInBytes, &resultSizeInBytes, &instanceDescsSize, instanceCount);

		if (instanceDescsSize == 0) {
			return false;
		}

		// Create the descriptor information for the tlas data
		m_instanceDescriptor.Fill(blasManager, instanceDescsSize, instances, instanceCount);

		// Rebuild the resource each time, as this was reccomended by NVIDIA as the best TLAS practice
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

		auto commandList = Commands::GetCommandList(Commands::COMPUTE);
		UINT64 scratchLocation;
		scratchBuffer->RequestSpace(commandList, scratchSizeInBytes, scratchLocation, false);

		D3D12_GPU_VIRTUAL_ADDRESS pSourceAS = 0;// updateOnly ? previousResult->GetGPUVirtualAddress() : 0;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC descriptor = {};
		descriptor.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		descriptor.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		descriptor.Inputs.InstanceDescs = m_instanceDescriptor.resource->GetGPUVirtualAddress();
		descriptor.Inputs.NumDescs = instanceCount;
		descriptor.Inputs.Flags = flags;

		descriptor.DestAccelerationStructureData = {
			resource->GetGPUVirtualAddress()
		};
		descriptor.ScratchAccelerationStructureData = {
			scratchBuffer->resource->GetGPUVirtualAddress() + scratchLocation
		};
		descriptor.SourceAccelerationStructureData = pSourceAS;

		// Build the top level AS
		commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			commandList->BuildRaytracingAccelerationStructure(&descriptor, 0, nullptr);

			// Wait for the buffer to complete setup.
			D3D12_RESOURCE_BARRIER uavBarrier;
			uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uavBarrier.UAV.pResource = resource.Get();
			uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			commandList->ResourceBarrier(1, &uavBarrier);

			fence.Signal(device, commandQueue);
		});

		return true;
	}

	void TopLevelAccelerationStructure::CacluateBufferSizes(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize, const UINT instanceCount) {
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
		m_blasManager(blasManager),
		m_scratch(DEFAULT_TLAS_SCRATCH_SIZE, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, L"TLAS Scratch Buffer"),
		m_tlas{ TopLevelAccelerationStructure(L"TLAS 0"), TopLevelAccelerationStructure(L"TLAS 1") }
	{
		Reset();
	}

	TLASManager::~TLASManager()
	{

	}

	void TLASManager::Reset()
	{
		m_instances.clear();
		m_instances.reserve(DEFAULT_TLAS_COUNT);
	}

	bool TLASManager::Generate()
	{
		auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COMPUTE);
		DX12Rendering::CaptureEventStart(commandList->GetCommandQueue(), "TLASManager::Generate");

		if (m_scratch.state != Resource::Ready)
		{
			m_scratch.Build();
		}

		bool result = m_tlas[GetCurrentFrameIndex()].UpdateResources(*m_blasManager, m_instances.data(), m_instances.size(), &m_scratch);

		DX12Rendering::CaptureEventEnd(commandList->GetCommandQueue());

		return result;
	}

	void TLASManager::AddInstance(const dxHandle_t& id, const float transform[16])
	{
		BottomLevelAccelerationStructure* blas = m_blasManager->GetBLAS(id);
		if (blas == nullptr)
		{
			return;
		}

		DX12Rendering::Instance* instance;
		if (TryGetWriteInstance(blas->id, &instance))
		{
			memcpy(instance->transformation, transform, sizeof(float[16]));
			return;
		}

		m_instances.emplace_back(transform, blas->id, 0 /* TODO: Find the hit group index containing the normal map of the surface. */);
	}

	const bool TLASManager::TryGetWriteInstance(const dxHandle_t& index, DX12Rendering::Instance** outInstance)
	{
		for (auto instance : m_instances) {
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
		ImGui::Text("Instance Count: %d", m_instances.size());

		// Each TLAS
		for (TopLevelAccelerationStructure& tlas : m_tlas)
		{
			char title[50];
			std::sprintf(title, "%ls", tlas.GetName());

			if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
			{
				tlas.ImGuiDebug();
			}
		}
	}
#endif
#pragma endregion
}