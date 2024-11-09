#ifndef __DX12_RESOURCE_H__
#define __DX12_RESOURCE_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"

namespace DX12Rendering {
	struct Resource
	{
		enum eResourceState
		{
			Unallocated = 0,
			Ready,
			Dirty,
			Removed,

			Count
		};

		ComPtr<ID3D12Resource> resource;
		eResourceState state;

		Resource(const LPCWSTR name = nullptr) :
			m_name(name == nullptr ? L"" : name),
			state(eResourceState::Unallocated)
		{
			
		}

		void Release();

		const LPCWSTR GetName() { return m_name.empty() ? nullptr : m_name.c_str(); }
		bool Exists() { return state >= Ready && state < Removed; }
		bool IsRemoved() { return state == Removed; }

		/// <summary>
		/// Uses the last known format state to define a Resource Barrier to the new state.
		/// </summary>
		/// <param name="toTransition">The transition to. If this is the same as the last know state, no barrier is made.</param>
		/// <param name="resourceBarrier">The barrier to store the stransition information in.</params>
		/// <returns>True if a transition was created.</returns>
		bool TryTransition(const D3D12_RESOURCE_STATES toTransition, D3D12_RESOURCE_BARRIER* resourceBarrier);

		virtual D3D12_UNORDERED_ACCESS_VIEW_DESC* GetUavDescriptorView() { return nullptr; }
		virtual D3D12_SHADER_RESOURCE_VIEW_DESC* GetSrvDescriptorView() { return nullptr; }

	protected:
		ID3D12Resource* Allocate(D3D12_RESOURCE_DESC& description, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps, const D3D12_CLEAR_VALUE* clearValue = nullptr);

	private:
		//TODO: define to only work with debug.
		const std::wstring m_name;
		D3D12_RESOURCE_STATES m_resourceState;
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
			m_defaultResourceState(resourceState),
			m_lastFenceValue(Commands::DIRECT, 0)
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

		bool IsFenceCompleted();
		void WaitForLastFenceToComplete();
		
		/// <summary>
		/// Resets the scratch buffer to a 0 index;
		/// </summary>
		void Reset(); 

	private:
		UINT64 m_currentIndex; // The next available space to fill the scratch buffer.
		const UINT m_alignment;
		const D3D12_HEAP_PROPERTIES m_heapProps;
		const D3D12_RESOURCE_FLAGS m_flags;
		const D3D12_RESOURCE_STATES m_defaultResourceState;

		DX12Rendering::Commands::FenceValue m_lastFenceValue;
	};

	struct ConstantBuffer
	{
		size_t offset;
		size_t size;

		D3D12_CONSTANT_BUFFER_VIEW_DESC bufferLocation;
	};

	class ResourceManager;

	ResourceManager* GetResourceManager();
	void DestroyResourceManager();
}

class DX12Rendering::ResourceManager
{
public:
	ResourceManager(const DX12Rendering::ResourceManager&) = delete;

	static const size_t MAX_CONSTANT_BUFFER_SIZE = 4096;

	ResourceManager();
	~ResourceManager();

	void Initialize();

	/// <summary>
	/// Constant buffer that is garunteed for the current frame.
	/// </summary>
	/// <param name="size">Size in bytes of the requested buffer.</param>
	/// <returns></returns>
	const ConstantBuffer RequestTemporyConstantBuffer(size_t size);
	void FillConstantBuffer(const ConstantBuffer& buffer, const void* data);

private:
	const size_t m_maxCBVHeapSize;

	ComPtr<ID3D12Resource> m_cbvUploadHeap;
	UINT m_nextConstantBufferOffset;
};
#endif