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