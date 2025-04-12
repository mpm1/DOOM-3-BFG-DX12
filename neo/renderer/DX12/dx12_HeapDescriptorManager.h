#ifndef __DX12_HEAP_DESCRIPTOR_MANAGER__
#define __DX12_HEAP_DESCRIPTOR_MANAGER__

#include "./dx12_global.h"

#define RENDER_OBJECTS_HEAP_START 0
#define RENDER_OBJECTS_HEAP_SIZE (MAX_OBJECT_COUNT * MAX_DESCRIPTOR_COUNT)

#define COMPUTE_OBJECTS_HEAP_START (RENDER_OBJECTS_HEAP_START + RENDER_OBJECTS_HEAP_SIZE)
#define COMPUTE_OBJECTS_HEAP_SIZE (MAX_OBJECT_COUNT * MAX_DESCRIPTOR_COUNT)

#define TEXTURE_ENTRIES_HEAP_START (COMPUTE_OBJECTS_HEAP_START + COMPUTE_OBJECTS_HEAP_SIZE)
#define TEXTURE_ENTRIES_HEAP_SIZE 4096

#define SAMPLER_HEAP_START 0
#define SAMPLERS_PER_OBJECT_MAX 16
#define SAMPLERS_HEAP_SIZE 2048 /*Current max. (MAX_OBJECT_COUNT * SAMPLERS_PER_OBJECT_MAX)*/

#define TOTAL_HEAP_SIZE (RENDER_OBJECTS_HEAP_SIZE + COMPUTE_OBJECTS_HEAP_SIZE + TEXTURE_ENTRIES_HEAP_SIZE)

namespace DX12Rendering
{
	enum eHeapDescriptorPartition
	{
		eHeapDescriptorRenderObjects = 0,
		eHeapDescriptorComputeObjects,
		eHeapDescriptorTextureEntries,
		eHeapDescriptorSamplerEntries
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
		ID3D12DescriptorHeap* GetSamplerHeap() { return m_samplerHeap.Get(); }

	private:
		UINT m_cbvHeapIncrementor;
		ComPtr<ID3D12DescriptorHeap> m_cbvHeap;

		UINT m_samplerHeapIncrementor;
		ComPtr< ID3D12DescriptorHeap> m_samplerHeap;

		void CreateCBVHeap();
		void CreateSamplerHeap();

		const UINT m_partitionIndexStart[4] = { // Must match the order in eHeapDescriptorPartition
			RENDER_OBJECTS_HEAP_START,
			COMPUTE_OBJECTS_HEAP_START,
			TEXTURE_ENTRIES_HEAP_START,
			SAMPLER_HEAP_START
		};

	};

	HeapDescriptorManager* GetDescriptorManager();
}

#endif
