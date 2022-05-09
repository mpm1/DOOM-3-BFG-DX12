#ifndef __DX12_RENDERER_H__
#define __DX12_RENDERER_H__

#include <map>
#include <functional>
#include <mutex>

#include "./dx12_global.h"
#include "./dx12_RootSignature.h"
#include "./dx12_raytracing.h"

// Use D3D clip space.
#define CLIP_SPACE_D3D

#define BUFFER_RGB 0x01
#define BUFFER_STENCIL 0x02

#define COMMAND_LIST_COUNT 5

using namespace DirectX;
using namespace Microsoft::WRL;

// TODO: Start setting frame data to it's own object to make it easier to manage.
struct DX12FrameDataBuffer
{
	// Render Data
	ComPtr<ID3D12Resource> renderTargets;

	// CBV Heap data
	ComPtr<ID3D12DescriptorHeap> cbvHeap;
	ComPtr<ID3D12Resource> cbvUploadHeap;
	UINT cbvHeapIndex;
	UINT8* m_constantBufferGPUAddress;
};

bool DX12_ActivatePipelineState();

class DX12Renderer {
public:
	DX12Renderer();
	~DX12Renderer();

	void Init(HWND hWnd);
	bool SetScreenParams(UINT width, UINT height, int fullscreen);
	void OnDestroy();

	void UpdateViewport(const FLOAT topLeftX, const FLOAT topLeftY, const FLOAT width, const FLOAT height, const FLOAT minDepth = 0.0f, const FLOAT maxDepth = 1.0f); // Used to put us into right hand depth space.
	void UpdateScissorRect(const LONG x, const LONG y, const LONG w, const LONG h);
	void UpdateStencilRef(UINT ref);

	void ReadPixels(int x, int y, int width, int height, UINT readBuffer, byte* buffer);

	// Shaders
	void LoadPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC* psoDesc, ID3D12PipelineState** ppPipelineState);
	void SetActivePipelineState(ID3D12PipelineState* pipelineState);

	void Uniform4f(UINT index, const float* uniform);

	// Buffers
	DX12VertexBuffer* AllocVertexBuffer(DX12VertexBuffer* buffer, UINT numBytes);
	void FreeVertexBuffer(DX12VertexBuffer* buffer);

	DX12IndexBuffer* AllocIndexBuffer(DX12IndexBuffer* buffer, UINT numBytes);
	void FreeIndexBuffer(DX12IndexBuffer* buffer);

	DX12JointBuffer* AllocJointBuffer(DX12JointBuffer* buffer, UINT numBytes);
	void FreeJointBuffer(DX12JointBuffer* buffer);
	void SetJointBuffer(DX12JointBuffer* buffer, UINT jointOffset);

	// Textures
	void SetActiveTextureRegister(UINT8 index);
	DX12TextureBuffer* AllocTextureBuffer(DX12TextureBuffer* buffer, D3D12_RESOURCE_DESC* textureDesc, const idStr* name);
	void FreeTextureBuffer(DX12TextureBuffer* buffer);
	void SetTextureContent(DX12TextureBuffer* buffer, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image);
	void SetTexture(DX12TextureBuffer* buffer);
	void StartTextureWrite(DX12TextureBuffer* buffer);
	void EndTextureWrite(DX12TextureBuffer* buffer);
	bool SetTextureCopyState(DX12TextureBuffer* buffer, const UINT mipLevel);
	bool SetTexturePixelShaderState(DX12TextureBuffer* buffer, const UINT mipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	bool SetTextureState(DX12TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, ID3D12GraphicsCommandList *commandList, const UINT mipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	// Draw commands
	void BeginDraw();
	void Clear(const bool color, const bool depth, bool stencil, byte stencilValue, const float colorRGBA[4]);
	void EndDraw();
	void PresentBackbuffer();
	void ResetCommandList(bool waitForBackBuffer = false);
	void SetCommandListDefaults(const bool resetPipelineState = true);
	void ExecuteCommandList();
	UINT StartSurfaceSettings(); // Starts a new heap entry for the surface.
	bool EndSurfaceSettings(); // Records the the surface entry into the heap.
	void DrawModel(DX12VertexBuffer* vertexBuffer, UINT vertexOffset, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);

	// Raytracing
	void ResetAccelerationStructure(); // Resets the bottom level acceleration structure to an empty state.
	void UpdateEntityInBLAS(const qhandle_t entityHandle, const renderEntity_t* re);
	void UpdateBLAS(); // Builds or rebuilds the bottom level acceleration struction based on its internal state.

	// Top Level Acceleration structure
	template<typename K>
	DX12Rendering::TopLevelAccelerationStructure* GetOrGenerateAccelerationStructure(const K* keyHandle);

	template<typename K, typename T>
	void AddEntityAccelerationStructure(const K* keyHandle, const T* entity);

	template<typename K>
	void UpdateAccelerationStructure(const K* keyHandle);

	bool IsRaytracingEnabled() const { return m_raytracing != nullptr && m_raytracing->isRaytracingSupported; };
	bool GenerateRaytracedStencilShadows(const dxHandle_t lightHandle);

	// Converters
	template<typename T>
	const dxHandle_t GetHandle(const T* qEntity);

private:
	UINT m_width;
	UINT m_height;
	int m_fullScreen; // 0 = windowed, otherwise 1 based monitor number to go full screen on
						// -1 = borderless window for spanning multiple displays

	FLOAT m_aspectRatio = 1.0f;
    FLOAT m_FoV = 90.0f;

	bool m_isDrawing = false;
	bool m_initialized = false;

	// Pipeline
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<ID3D12Device5> m_device;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize;
	ComPtr<ID3D12Resource> m_renderTargets[DX12_FRAME_COUNT];
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12Resource> m_depthBuffer;
	DX12RootSignature* m_rootSignature;

	// Command List
	ComPtr<ID3D12CommandQueue> m_directCommandQueue;
	ComPtr<ID3D12CommandQueue> m_copyCommandQueue;
	ComPtr<ID3D12CommandAllocator> m_directCommandAllocator[DX12_FRAME_COUNT];
	ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
	ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;

	XMFLOAT4 m_constantBuffer[53];
	UINT8* m_constantBufferGPUAddress[DX12_FRAME_COUNT];
	ID3D12PipelineState* m_activePipelineState = nullptr;
	UINT m_stencilRef = 0;

	UINT m_objectIndex = 0;

	// Synchronization
	UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT16 m_fenceValue;
	HANDLE m_copyFenceEvent;
	ComPtr<ID3D12Fence> m_copyFence;
	UINT16 m_copyFenceValue;

	// Textures
	ComPtr<ID3D12Resource> m_textureBufferUploadHeap;
	UINT8 m_activeTextureRegister;
	DX12TextureBuffer* m_activeTextures[TEXTURE_REGISTER_COUNT];

	// Device removal
	HANDLE m_deviceRemovedHandle;
	HANDLE m_removeDeviceEvent;

	// Raytracing
	DX12Rendering::Raytracing* m_raytracing;
	std::mutex m_raytracingMutex;

	void LoadPipeline(HWND hWnd);
	void LoadAssets();

	void SignalNextFrame();
    void WaitForPreviousFrame();
	void WaitForCopyToComplete();

	bool CreateBackBuffer();

	bool IsScissorWindowValid();

#ifdef DEBUG_IMGUI
	ComPtr<ID3D12DescriptorHeap> m_imguiSrvDescHeap;

	void InitializeImGui(HWND hWnd);
	void ReleaseImGui();
	void ImGuiDebugWindows();
#endif
};

extern DX12Renderer dxRenderer;

#endif