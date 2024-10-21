#pragma hdrstop

#include "./dx12_resource.h"
#include "./dx12_DeviceManager.h"
#include <assert.h>

namespace DX12Rendering {
	ID3D12Resource* Resource::Allocate(D3D12_RESOURCE_DESC& description, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps, const D3D12_CLEAR_VALUE* clearValue)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		if(device == nullptr)
		{ 
			return nullptr;
		}

		assert(state == Unallocated || state >= Removed);
		assert(description.Width != 0 && description.Height != 0);

		if (!DX12Rendering::WarnIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &description, initState, clearValue, IID_PPV_ARGS(&resource))))
		{
			state = Unallocated;
			return nullptr;
		}

		if (!m_name.empty())
		{
			resource->SetName(GetName());
		}

		m_resourceState = initState;
		state = Ready;
		return resource.Get();
	}

	void Resource::Release()
	{
		if (Exists())
		{
			resource = nullptr;
			state = Unallocated;
		}
	}

	bool Resource::TryTransition(const D3D12_RESOURCE_STATES toTransition, D3D12_RESOURCE_BARRIER* resourceBarrier)
	{
		if (!Exists() || // No resource to tansition on.
			m_resourceState == toTransition || // No state change to transition to.
			resourceBarrier == nullptr) // No place to store the barrier.
		{
			return false;
		}

		*resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), m_resourceState, toTransition);
		m_resourceState = toTransition;

		return true;
	}

#pragma region ScratchBuffer
	ID3D12Resource* ScratchBuffer::Build()
	{
		D3D12_RESOURCE_DESC description = {};
		description.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		description.DepthOrArraySize = 1;
		description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		description.Flags = m_flags;
		description.Format = DXGI_FORMAT_UNKNOWN;
		description.Height = 1;
		description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		description.MipLevels = 1;
		description.SampleDesc.Count = 1;
		description.SampleDesc.Quality = 0;
		description.Width = m_size;

		return Allocate(description, m_defaultResourceState, m_heapProps);
	}

	bool ScratchBuffer::RequestSpace(DX12Rendering::Commands::CommandList* commandList, const UINT64 size, UINT64& offset, bool waitForSpace)
	{
		// Allocate the resource on the GPU if needed.
		if (state == Unallocated || state >= Removed)
		{
			if (!Build())
			{
				return false;
			}
		}

		if (waitForSpace)
		{
			if (commandList->IsFenceComplete(m_lastFenceValue))
			{
				state = Ready;
			}
			else
			{
				offset = 0;
				return false;
			}
		}		

		// For now we're keeping it simple and just allowing the buffer to loop.
		const UINT64 alignedSize = DX12_ALIGN(size, m_alignment);

		offset = m_currentIndex;
		m_currentIndex += alignedSize;
		if (m_currentIndex >= m_size)
		{
			offset = 0;
			m_currentIndex = 0;

			m_lastFenceValue = commandList->AddPostFenceSignal();

			if (waitForSpace)
			{
				state = Dirty;
				return false;
			}
		}

		return true;
	}

	void ScratchBuffer::WaitForLastFenceToComplete()
	{
		if (m_lastFenceValue.value > 0)
		{
			return DX12Rendering::Commands::GetCommandManager(m_lastFenceValue.commandList)->WaitOnFence(m_lastFenceValue);
		}
	}

	bool ScratchBuffer::IsFenceCompleted()
	{
		if (m_lastFenceValue.value > 0)
		{
			return DX12Rendering::Commands::GetCommandManager(m_lastFenceValue.commandList)->IsFenceCompleted(m_lastFenceValue);
		}

		return true;
	}
#pragma endregion

#pragma region ResourceManager
	ResourceManager* m_resourceManager = nullptr;

	ResourceManager* GetResourceManager()
	{
		if (m_resourceManager == nullptr)
		{
			m_resourceManager = new ResourceManager();
		}

		return m_resourceManager;
	}

	void DestroyResourceManager()
	{
		if (m_resourceManager)
		{
			delete m_resourceManager;
			m_resourceManager = nullptr;
		}
	}

	ResourceManager::ResourceManager()
		: m_nextConstantBufferOffset(0)
		, m_maxCBVHeapSize(DX12_ALIGN(CBV_REGISTER_COUNT * MAX_OBJECT_COUNT * MAX_CONSTANT_BUFFER_SIZE * DX12_FRAME_COUNT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)) // 64kb aligned
	{
		auto device = DX12Rendering::Device::GetDevice();

		assert(device != nullptr);

		const UINT heapSize = m_maxCBVHeapSize;

		DX12Rendering::ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, // Currently not clear value needed
			IID_PPV_ARGS(&m_cbvUploadHeap)
		));

		m_cbvUploadHeap->SetName(L"CBV Temporary Upload");
	}

	ResourceManager::~ResourceManager()
	{
		m_cbvUploadHeap = nullptr;
	}

	const ConstantBuffer ResourceManager::RequestTemporyConstantBuffer(size_t size)
	{
		const UINT bufferSize = DX12_ALIGN(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

		ConstantBuffer buffer = {};

		if (m_nextConstantBufferOffset + bufferSize >= m_maxCBVHeapSize)
		{
			buffer.offset = 0; // Reset to the beginning
		}
		else
		{
			buffer.offset = m_nextConstantBufferOffset;
		}

		buffer.size = bufferSize;

		buffer.bufferLocation.BufferLocation = m_cbvUploadHeap->GetGPUVirtualAddress() + buffer.offset;
		buffer.bufferLocation.SizeInBytes = buffer.size;

		m_nextConstantBufferOffset = buffer.offset + buffer.size; // If we go over the max allowed, the next request will go back to 0

		return buffer;
	}

	void ResourceManager::FillConstantBuffer(const ConstantBuffer& buffer, const void* data)
	{
		// Copy the CBV value to the upload heap
		UINT8* mappedBuffer;
		CD3DX12_RANGE readRange(buffer.offset, buffer.size);

		DX12Rendering::ThrowIfFailed(m_cbvUploadHeap->Map(0, &readRange, reinterpret_cast<void**>(&mappedBuffer)));
		memcpy(&mappedBuffer[buffer.offset], data, buffer.size);
		m_cbvUploadHeap->Unmap(0, &readRange);
	}
#pragma engregion
}