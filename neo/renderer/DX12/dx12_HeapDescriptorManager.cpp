#pragma hdrstop

#include "./dx12_HeapDescriptorManager.h"
#include "./dx12_DeviceManager.h"

using namespace DX12Rendering;

namespace
{
	HeapDescriptorManager m_heapDescriptorManager;
}

HeapDescriptorManager* DX12Rendering::GetDescriptorManager()
{
	if (!m_heapDescriptorManager.IsInitialized())
	{
		m_heapDescriptorManager.Initialize();
	}

	return &m_heapDescriptorManager;
}

HeapDescriptorManager::HeapDescriptorManager() :
	m_cbvHeap(nullptr)
{

}

HeapDescriptorManager::~HeapDescriptorManager()
{
	if (m_cbvHeap)
	{
		m_cbvHeap->Release();
		m_cbvHeap = nullptr;
	}
}

void HeapDescriptorManager::Initialize()
{
	if (IsInitialized()) {
		return;
	}

	CreateCBVHeap();
}

void HeapDescriptorManager::CreateCBVHeap() {
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	// Create the buffer size.
	WCHAR heapName[30];

	// Describe and create the constant buffer view (CBV) descriptor
	{
		// Create the CBV Heap
		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
		cbvHeapDesc.NumDescriptors = TOTAL_HEAP_SIZE;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX12Rendering::ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));

		wsprintfW(heapName, L"CBV Heap %d", 1); // TODO: We should do this for each frame.
		m_cbvHeap->SetName(heapName);
	}

	m_cbvHeapIncrementor = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE HeapDescriptorManager::GetGPUDescriptorHandle(const eHeapDescriptorPartition partition, const UINT heapIndex)
{
	UINT offset = heapIndex + m_partitionIndexStart[partition];

	ID3D12DescriptorHeap* heap = m_cbvHeap.Get();
	UINT incrementor = m_cbvHeapIncrementor;

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle(heap->GetGPUDescriptorHandleForHeapStart(), offset, incrementor);

	return gpuDescriptorHandle;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE HeapDescriptorManager::GetCPUDescriptorHandle(const eHeapDescriptorPartition partition, const UINT heapIndex)
{

	UINT offset = heapIndex + m_partitionIndexStart[partition];

	ID3D12DescriptorHeap* heap = m_cbvHeap.Get();
	UINT incrementor = m_cbvHeapIncrementor;

	CD3DX12_CPU_DESCRIPTOR_HANDLE gpuDescriptorHandle(heap->GetCPUDescriptorHandleForHeapStart(), offset, incrementor);

	return gpuDescriptorHandle;
}
