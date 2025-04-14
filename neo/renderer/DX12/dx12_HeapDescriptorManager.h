#ifndef __DX12_HEAP_DESCRIPTOR_MANAGER__
#define __DX12_HEAP_DESCRIPTOR_MANAGER__

#include "./dx12_global.h"

#define RENDER_OBJECTS_HEAP_START 0
#define RENDER_OBJECTS_HEAP_SIZE (MAX_OBJECT_COUNT * MAX_DESCRIPTOR_COUNT)

#define COMPUTE_OBJECTS_HEAP_START (RENDER_OBJECTS_HEAP_START + RENDER_OBJECTS_HEAP_SIZE)
#define COMPUTE_OBJECTS_HEAP_SIZE (MAX_OBJECT_COUNT * MAX_DESCRIPTOR_COUNT)

#define TEXTURE_ENTRIES_HEAP_START (COMPUTE_OBJECTS_HEAP_START + COMPUTE_OBJECTS_HEAP_SIZE)
#define TEXTURE_ENTRIES_HEAP_SIZE 4096

#define TOTAL_HEAP_SIZE (RENDER_OBJECTS_HEAP_SIZE + COMPUTE_OBJECTS_HEAP_SIZE + TEXTURE_ENTRIES_HEAP_SIZE)

namespace DX12Rendering
{
	enum eHeapDescriptorPartition
	{
		eHeapDescriptorRenderObjects = 0,
		eHeapDescriptorComputeObjects,
		eHeapDescriptorTextureEntries
	};

	class HeapDescriptorManager
	{
	public:
		HeapDescriptorManager();
		~HeapDescriptorManager();

		void Initialize();

		CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const eHeapDescriptorPartition partition, const UINT heapIndex);
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const eHeapDescriptorPartition partition, const UINT heapIndex);

		bool IsInitialized() { return m_cbvHeap != nullptr; }

		ID3D12DescriptorHeap* GetCBVHeap() { return m_cbvHeap.Get(); }

	private:
		UINT m_cbvHeapIncrementor;
		ComPtr<ID3D12DescriptorHeap> m_cbvHeap;

		void CreateCBVHeap();

		const UINT m_partitionIndexStart[4] = { // Must match the order in eHeapDescriptorPartition
			RENDER_OBJECTS_HEAP_START,
			COMPUTE_OBJECTS_HEAP_START,
			TEXTURE_ENTRIES_HEAP_START
		};

	};

	HeapDescriptorManager* GetDescriptorManager();
}

#endif
