#ifndef __DX12_RESOURCE_H__
#define __DX12_RESOURCE_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"

namespace DX12Rendering {
	struct Resource
	{
		enum ResourceState
		{
			Unallocated = 0,
			Ready,
			Dirty,
			Removed,

			Count
		};

		Fence fence;
		ComPtr<ID3D12Resource> resource;
		ResourceState state;

		Resource(const LPCWSTR name = nullptr) :
			m_name(name == nullptr ? L"" : name),
			state(ResourceState::Unallocated) 
		{
			
		}

		void Release();

		const LPCWSTR GetName() { return m_name.empty() ? nullptr : m_name.c_str(); }
		bool Exists() { return state >= Ready && state < Removed; }
		bool IsRemoved() { return state == Removed; }

	protected:
		ID3D12Resource* Allocate(D3D12_RESOURCE_DESC& description, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);

	private:
		//TODO: define to only work with debug.
		const std::wstring m_name;
	};

	struct ScratchBuffer : public Resource
	{
		const UINT64 m_size; // The total memory size of the buffer

		ScratchBuffer(UINT64 size, UINT alignment, D3D12_HEAP_PROPERTIES heapProps, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES resourceState, LPCWSTR name) :
			Resource(name),
			m_size(DX12_ALIGN(size, alignment)),
			m_currentIndex(0),
			m_alignment(alignment),
			m_heapProps(heapProps),
			m_flags(flags),
			m_resourceState(resourceState)
		{}

		ID3D12Resource* Build();

		/// <summary>
		/// Reserves space on the buffer to be used immediately.
		/// </summary>
		/// <param name="commandList">The command list used for any actions allocating the space.</param>
		/// <param name="size">The size of space to allocate.</param>
		/// <param name="offset">The resulting offset to the scratch buffer to store the data.</param>
		/// <returns>True if we were able to allocate the space. False otherwise.</returns>
		bool RequestSpace(DX12Rendering::Commands::CommandList* commandList, const UINT64 size, UINT64& offset, bool waitForSpace = true);

	private:
		UINT64 m_currentIndex; // The next available space to fill the scratch buffer.
		const UINT m_alignment;
		const D3D12_HEAP_PROPERTIES m_heapProps;
		const D3D12_RESOURCE_FLAGS m_flags;
		const D3D12_RESOURCE_STATES m_resourceState;
	};
}

#endif