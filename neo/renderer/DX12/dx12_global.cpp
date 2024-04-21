#pragma hdrstop

#include "./dx12_global.h"
#include "../../idlib/precompiled.h"
#include "./dx12_CommandList.h"

#include <comdef.h>

extern idCommon* common;

using namespace Microsoft::WRL;

#if defined(USE_PIX)
namespace
{
	
}

void DX12Rendering::CaptureEventStart(Commands::CommandList* commandList, const std::string message)
{
	commandList->AddPreExecuteQueueAction([message](ID3D12CommandQueue* commandQueue)
	{
		PIXBeginEvent(commandQueue, PIX_COLOR(128, 255, 128), message.c_str());
	});
};

void DX12Rendering::CaptureEventEnd(Commands::CommandList* commandList) {
	commandList->AddPostExecuteQueueAction([](ID3D12CommandQueue* commandQueue)
	{
		PIXEndEvent(commandQueue);
	});
}

void DX12Rendering::CaptureGPUBegin() 
{
	PIXCaptureParameters captureParams = {};

	captureParams.GpuCaptureParameters.FileName = L"GPUCapture.wpix";

	/*captureParams.TimingCaptureParameters.CaptureGpuTiming = TRUE;
	captureParams.TimingCaptureParameters.CaptureCallstacks = TRUE;
	captureParams.TimingCaptureParameters.CaptureCpuSamples = FALSE;
	captureParams.TimingCaptureParameters.CpuSamplesPerSecond = 4000;

	captureParams.TimingCaptureParameters.CaptureStorage = PIXCaptureParameters::Memory;
	captureParams.TimingCaptureParameters.FileName = L"GPUCapture.wpix";
	captureParams.TimingCaptureParameters.MaximumToolingMemorySizeMb = 4096;*/

	PIXBeginCapture(PIX_CAPTURE_GPU, &captureParams);
}

void DX12Rendering::CaptureGPUEnd(bool discard) { PIXEndCapture(discard);  };
#else
void DX12Rendering::CaptureEventStart(Commands::CommandList* commandList, const std::string message) {};
void DX12Rendering::CaptureEventEnd(Commands::CommandList* commandList) {}
void DX12Rendering::CaptureGPUBegin() {};
void DX12Rendering::CaptureGPUEnd(bool discard) {};
#endif

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
	assert(size != 0);

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
	desc.NumDescriptors = count * DX12_FRAME_COUNT; // Have a full set of descriptors per frame.
	desc.Type = type;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ID3D12DescriptorHeap* heap;
	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));

	return heap;
}

UINT8 g_frameIndex = 0;
const UINT8 DX12Rendering::GetCurrentFrameIndex() { return g_frameIndex; }
const UINT8 DX12Rendering::GetLastFrameIndex() { return g_frameIndex == 0 ? DX12_FRAME_COUNT - 1 : g_frameIndex - 1; }
const UINT8 DX12Rendering::GetNextFrameIndex() { return g_frameIndex == DX12_FRAME_COUNT - 1 ? 0 : g_frameIndex + 1; }
const UINT8 DX12Rendering::UpdateFrameIndex(IDXGISwapChain3* swapChain) // This should only be used by dxRenderer
{
	g_frameIndex = swapChain->GetCurrentBackBufferIndex();

	return g_frameIndex;
}