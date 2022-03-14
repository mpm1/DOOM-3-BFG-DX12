#pragma hdrstop

#include "./dx12_global.h"
#include "../../idlib/precompiled.h"

#include <comdef.h>

extern idCommon* common;

using namespace Microsoft::WRL;

void DX12Rendering::FailMessage(LPCSTR message) {
	OutputDebugString(message);
	common->Error(message);
}

void DX12Rendering::WarnMessage(LPCSTR message) {
	OutputDebugString(message);
	common->Warning(message);
}

void DX12Rendering::ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		_com_error err(hr);
		auto errDesc = err.ErrorInfo();

		// Set a breakpoint on this line to catch DirectX API errors
		DX12Rendering::FailMessage(err.ErrorMessage());

		throw std::exception(err.ErrorMessage());
	}
}

bool DX12Rendering::WarnIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		_com_error err(hr);
		// Set a breakpoint on this line to catch DirectX API errors
		common->Warning(err.ErrorMessage());
		return false;
	}

	return true;
}

ID3D12Resource* DX12Rendering::CreateBuffer(ID3D12Device5* device, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps) {
	D3D12_RESOURCE_DESC description = {};
	description.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	description.DepthOrArraySize = 1;
	description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	description.Flags = flags;
	description.Format = DXGI_FORMAT_UNKNOWN;
	description.Height = 1;
	description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	description.MipLevels = 1;
	description.SampleDesc.Count = 1;
	description.SampleDesc.Quality = 0;
	description.Width = size;

	ID3D12Resource* pBuffer;
	DX12Rendering::ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &description, initState, nullptr, IID_PPV_ARGS(&pBuffer)));

	return pBuffer;
}

ID3D12DescriptorHeap* DX12Rendering::CreateDescriptorHeap(ID3D12Device* device, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = count;
	desc.Type = type;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ID3D12DescriptorHeap* heap;
	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));

	return heap;
}