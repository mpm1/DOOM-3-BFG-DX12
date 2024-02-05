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
			if (fence.IsFenceCompleted())
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

			commandList->AddPostFenceSignal(&fence);

			if (waitForSpace)
			{
				state = Dirty;
				return false;
			}
		}

		return true;
	}
#pragma endregion
}