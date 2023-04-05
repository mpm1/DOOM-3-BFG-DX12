#ifndef __DX12_GLOBAL_H__
#define __DX12_GLOBAL_H__

#include <wrl.h>
#include <initguid.h>
#include "./d3dx12.h"
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <debugapi.h>
#include <dxcapi.h>
#include <DirectXMath.h>
#include <vector>
#include <shared_mutex>

#pragma comment (lib, "dxguid.lib")
#pragma comment (lib, "d3d12.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "dxcompiler.lib")

#ifdef _DEBUG
#include "./debug/dx12_debug.h"
#include "dx12_imgui.h"

#ifdef DEBUG_PIX
#undef _vsnprintf
#include <pix3.h>
#define _vsnprintf	use_idStr_vsnPrintf

#include <DXProgrammableCapture.h>

#elif defined(DEBUG_NSIGHTS)
#else
#define DEBUG_GPU
#endif

#endif

#define DX12_FRAME_COUNT 2

#define TEXTURE_REGISTER_COUNT 5
#define MAX_DESCRIPTOR_COUNT 7 // 2 CBV and 5 Shader Resource View
#define MAX_OBJECT_COUNT 10000
#define MAX_HEAP_INDEX_COUNT 70000

#define DX12_ALIGN(SIZE, ALIGNMENT) ((SIZE) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1)

using namespace DirectX;
using namespace Microsoft::WRL;

typedef UINT64 dxHandle_t;

namespace DX12Rendering {
	namespace Commands
	{
		class CommandList;
	}

	// Debug Captures
	void CaptureEventStart(Commands::CommandList* commandList, const std::string message);
	void CaptureEventEnd(Commands::CommandList* commandList);
	void CaptureGPUBegin();
	void CaptureGPUEnd(bool discard);

	struct CaptureEventBlock
	{
		CaptureEventBlock(DX12Rendering::Commands::CommandList* commandList, const std::string message) :
			m_commandList(commandList)
		{
			DX12Rendering::CaptureEventStart(m_commandList, message);
		}

		~CaptureEventBlock()
		{
			DX12Rendering::CaptureEventEnd(m_commandList);
		}

	private:
		DX12Rendering::Commands::CommandList* m_commandList;
	};

	// Error messages
	void FailMessage(LPCSTR message);
	void WarnMessage(LPCSTR message);
	void ThrowIfFailed(HRESULT hr);
	bool WarnIfFailed(HRESULT hr);

	static const DWORD FENCE_MAX_WAIT = INFINITE;

	// From NVIDIAs DXRHelper code
	// Specifies a heap used for uploading. This heap type has CPU access optimized
	// for uploading to the GPU.
	static const D3D12_HEAP_PROPERTIES kUploadHeapProps = {
		D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

	// Specifies the default heap. This heap type experiences the most bandwidth for
	// the GPU, but cannot provide CPU access.
	static const D3D12_HEAP_PROPERTIES kDefaultHeapProps = {
		D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

	ID3D12Resource* CreateBuffer(ID3D12Device5* device, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);

	ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible);

	// FrameIndexing
	const UINT8 GetCurrentFrameIndex();
	const UINT8 GetLastFrameIndex();
	const UINT8 IncrementFrameIndex();

	// Locking
	typedef std::shared_mutex dx12_lock;
	typedef std::unique_lock<dx12_lock> ReadLock;
	typedef std::shared_lock<dx12_lock> WriteLock;

	class Fence
	{
	public:
		Fence() : 
			m_value(0), m_fence(nullptr), m_fenceEvent(nullptr)
		{

		}

		~Fence()
		{
			if (m_fenceEvent != nullptr)
			{
				CloseHandle(m_fenceEvent);
			}
		}

		void Invalidate()
		{
			m_value = 1;
		}

		HRESULT Allocate(ID3D12Device5* device)
		{
			HRESULT result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
			m_value = 1;

			m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

			return result;
		}

		HRESULT Signal(ID3D12Device5* device, ID3D12CommandQueue* commandQueue)
		{
			HRESULT result;

			if (m_fence == nullptr)
			{
				result = Allocate(device);

				if (FAILED(result))
				{
					return result;
				}
			}

			++m_value;
			return commandQueue->Signal(m_fence.Get(), m_value);
		}

		HRESULT SetCompletionEvent(UINT64 value, HANDLE completionEvent)
		{
			return m_fence->SetEventOnCompletion(value, completionEvent);
		}

		bool IsFenceCompleted()
		{
			if (m_fence == nullptr)
			{
				// No fence to evaluate.
				return true;
			}

			UINT64 completedValue = m_fence->GetCompletedValue();
			return completedValue >= m_value;
		}

		void GPUWait(ID3D12CommandQueue* commandQueue)
		{
			if (m_fence == nullptr)
			{
				return;
			}

			WarnIfFailed(commandQueue->Wait(m_fence.Get(), m_value));
		}

		void Wait()
		{
			WaitLimitted(FENCE_MAX_WAIT);
		}

		void WaitLimitted(DWORD waitTime)
		{
			if (m_fence == nullptr)
			{
				return;
			}

			if (!IsFenceCompleted())
			{
				HRESULT result = SetCompletionEvent(m_value, m_fenceEvent);
				DX12Rendering::WarnIfFailed(result);
				WaitForSingleObject(m_fenceEvent, waitTime);
			}
		}
	private:
		UINT16 m_value;
		ComPtr<ID3D12Fence> m_fence;
		HANDLE m_fenceEvent;
	};
}

struct Vertex
{
	XMFLOAT4 position;
	XMFLOAT4 colour;
};

enum eStageType {
	DEPTH_STAGE,
};

struct DX12Stage
{
	eStageType type;

	UINT textureCount;
	//DX12TextureBuffer* textures[TEXTURE_REGISTER_COUNT];

	// TODO: Include stage information to tell if this is for shadow testing or lighting.
};

#endif