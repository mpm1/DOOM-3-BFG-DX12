#ifndef __DX12_ROOT_SIGNATURE_H__
#define __DX12_ROOT_SIGNATURE_H__

#include <wrl.h>
#include "./d3dx12.h"

using namespace Microsoft::WRL;

// TODO: Create separate file for Buffers
struct DX12JointBuffer
{
	ComPtr<ID3D12Resource> jointBuffer;
	UINT entrySizeInBytes;
};

HRESULT DX12CreateRootSignature(ID3D12Device5 *device, REFIID riid, _COM_Outptr_  void** ppvRootSignature);

// Joint buffer functions
HRESULT DX12AllocJointBuffer(ID3D12Device* device, DX12JointBuffer* buffer, const UINT numBytes, LPCWSTR heapName);
void DX12SetJointDescriptorTable(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12DescriptorHeap* jointHeap, DX12JointBuffer* buffer, UINT jointOffset, UINT heapIndex, UINT heapIncrementor);

#endif