#ifndef __DX12_RENDERER_H__
#define __DX12_RENDERER_H__

#include "./dx12_global.h"
#include "./dx12_RootSignature.h"
#include "./dx12_raytracing.h"

// Will be automatically enabled with preprocessor symbols: USE_PIX, DBG, _DEBUG, PROFILE, or PROFILE_BUILD
#include <pix3.h>

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

enum eShader {
	VERTEX,
	PIXEL
};

bool DX12_ActivatePipelineState();

class DX12Renderer {
public:
	DX12Renderer();
	~DX12Renderer();

	void Init(HWND hWnd);
	bool SetScreenParams(UINT width, UINT height, int fullscreen);
	void OnDestroy();

	void UpdateViewport(FLOAT topLeftX, FLOAT topLeftY, FLOAT width, FLOAT height, FLOAT minDepth = 0.0f, FLOAT maxDepth = 1.0f); // Used to put us into right hand depth space.
	void UpdateScissorRect(LONG left, LONG top, LONG right, LONG bottom);
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
	void SetJointBuffer(DX12JointBuffer* buffer, UINT jointOffset, DX12Object* storedObject);

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
	void Clear(bool color, bool depth, bool stencil, byte stencilValue, float* colorRGBA);
	void EndDraw();
	void PresentBackbuffer();
	void ResetCommandList(bool waitForBackBuffer = false);
	void ExecuteCommandList();
	UINT StartSurfaceSettings(); // Starts a new heap entry for the surface.
	bool EndSurfaceSettings(DX12Object* storedObject); // Records the the surface entry into the heap.
	void DrawModel(DX12VertexBuffer* vertexBuffer, UINT vertexOffset, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);

	// Raytracing
	void BeginRayTracingSetup();
	void EndRayTracingSetup();
	bool IsRaytracingEnabled() { return m_raytracing != nullptr && m_raytracing->isRaytracingSupported; };

	//Object commands
	DX12Object* AddToObjectList(DX12VertexBuffer* vertexBuffer, UINT vertexOffset, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);
	DX12Stage* AddStageToObject(DX12Object* storedObject, eStageType stageType, UINT textureCount, DX12TextureBuffer** textures);

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

	UINT m_objectCount = 0;
	UINT m_objectIndex = 0; //TODO: This will be removed once we put everything into the m_objects table.
	DX12Object m_objects[MAX_OBJECT_COUNT];

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

	void LoadPipeline(HWND hWnd);
	void LoadAssets();

	void SignalNextFrame();
    void WaitForPreviousFrame();
	void WaitForCopyToComplete();

	bool CreateBackBuffer();

	bool IsScissorWindowValid();
};

extern DX12Renderer dxRenderer;

#endif