#pragma hdrstop

#include "./dx12_global.h"
#include "../../idlib/precompiled.h"

#include <comdef.h>

extern idCommon* common;

using namespace Microsoft::WRL;

void DX12FailMessage(LPCSTR message) {
	OutputDebugString(message);
	common->Error(message);
}

void DX12ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		_com_error err(hr);

		// Set a breakpoint on this line to catch DirectX API errors
		DX12FailMessage(err.ErrorMessage());

		throw std::exception(err.ErrorMessage());
	}
}

bool DX12WarnIfFailed(HRESULT hr)
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