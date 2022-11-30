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

#include "dx12_imgui.h"

#ifdef DEBUG_PIX
//#include <pix3.h>
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
//#ifdef USE_PIX
//MARK figue out why pix is not working.
//	void CaptureEventStart(ID3D12CommandList* commandList, std::string message) { PIXBeginEvent(commandList, PIX_COLOR(128, 255, 128), message.c_str()); };
//	void CaptureEventEnd(ID3D12CommandList* commandList) { PIXEndEvent(commandList); }
//#else
//	void CaptureEventStart(ID3D12CommandList* commandList, std::string message) {};
//	void CaptureEventEnd(ID3D12CommandList* commandList) {}
//#endif

	void FailMessage(LPCSTR message);
	void WarnMessage(LPCSTR message);
	void ThrowIfFailed(HRESULT hr);
	bool WarnIfFailed(HRESULT hr);

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
			m_fence->SetEventOnCompletion(value, completionEvent);
		}

		bool IsFenceCompleted()
		{
			assert(m_fence != nullptr);

			return m_fence->GetCompletedValue() >= m_value;
		}

		void Wait()
		{
			WaitLimitted(INFINITE);
		}

		void WaitLimitted(DWORD waitTime)
		{
			if (m_fence == nullptr)
			{
				return;
			}

			if (!IsFenceCompleted())
			{
				DX12Rendering::WarnIfFailed(SetCompletionEvent(m_value, m_fenceEvent));
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

struct DX12VertexBuffer
{
	ComPtr<ID3D12Resource> vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
};

struct DX12IndexBuffer
{
	ComPtr<ID3D12Resource> indexBuffer;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	UINT indexCount;
};

struct DX12TextureBuffer
{
	ComPtr<ID3D12Resource> textureBuffer;
	D3D12_SHADER_RESOURCE_VIEW_DESC textureView;
	D3D12_RESOURCE_STATES usageState;
	const idStr* name;
};

struct DX12JointBuffer
{
	ComPtr<ID3D12Resource> jointBuffer;
	UINT entrySizeInBytes;
};

enum eStageType {
	DEPTH_STAGE,
};

struct DX12Stage
{
	eStageType type;

	UINT textureCount;
	DX12TextureBuffer* textures[TEXTURE_REGISTER_COUNT];

	// TODO: Include stage information to tell if this is for shadow testing or lighting.
};

#endif