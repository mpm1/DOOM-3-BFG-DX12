#ifndef __DX12_RENDERER_H__
#define __DX12_RENDERER_H__

#include <map>
#include <functional>
#include <mutex>

#include "./dx12_global.h"
#include "./dx12_CommandList.h"
#include "./dx12_RootSignature.h"
#include "./dx12_raytracing.h"

// Use D3D clip space.
#define CLIP_SPACE_D3D

#define BUFFER_RGB 0x01
#define BUFFER_STENCIL 0x02

#define COMMAND_LIST_COUNT 5

using namespace DirectX;
using namespace Microsoft::WRL;

class idRenderModel;

#ifdef _DEBUG
struct viewLight_t;
#endif

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
	bool SetTextureState(DX12TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList, const UINT mipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	// Draw commands
	void BeginDraw();
	void Clear(const bool color, const bool depth, bool stencil, byte stencilValue, const float colorRGBA[4]);
	void EndDraw();
	void PresentBackbuffer();
	void SetCommandListDefaults(const bool resetPipelineState = true);
	void CycleDirectCommandList();
	UINT StartSurfaceSettings(); // Starts a new heap entry for the surface.
	bool EndSurfaceSettings(); // Records the the surface entry into the heap.
	void DrawModel(DX12VertexBuffer* vertexBuffer, UINT vertexOffset, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);

#pragma region RayTracing
	void DXR_ResetAccelerationStructure(); // Resets the bottom level acceleration structure to an empty state.
	void DXR_UpdateModelInBLAS(const qhandle_t modelHandle, const idRenderModel* model);
	void DXR_UpdateBLAS(); // Builds or rebuilds the bottom level acceleration struction based on its internal state.

	void DXR_SetRenderParam(DX12Rendering::dxr_renderParm_t param, const float* uniform);
	void DXR_SetRenderParams(DX12Rendering::dxr_renderParm_t param, const float* uniform, const UINT count);

	bool DXR_CastRays(); // Sets the appropriate matricies and casts the rays to the scene.

	void DXR_DenoiseResult(); // Performs a Denoise pass on all rendering channels.
	void DXR_GenerateResult(); // Collapses all channels into a single image.
	void DXR_CopyResultToDisplay(); // Copies the resulting image to the user display.

#pragma region Top Level Acceleration Structure
	template<typename K>
	DX12Rendering::TopLevelAccelerationStructure* DXR_GetOrGenerateAccelerationStructure(const K& keyHandle);

	/// <summary>
	/// 
	/// </summary>
	/// <typeparam name="K"></typeparam>
	/// <param name="keyHandle"></param>
	/// <param name="entity"></param>
	/// <param name="modelMatrix"></param>
	template<typename K>
	void DXR_AddEntityAccelerationStructure(const K& keyHandle, const dxHandle_t entityHandle, const float modelMatrix[16]);

	template<typename K>
	void DXR_UpdateAccelerationStructure(const K& keyHandle);
#pragma endregion

	bool IsRaytracingEnabled() const { return m_raytracing != nullptr && m_raytracing->isRaytracingSupported; };
#pragma endregion

	// Converters
	template<typename T>
	const dxHandle_t GetHandle(const T* qEntity);

#ifdef _DEBUG
	void DebugAddLight(const viewLight_t& light);
	void DebugClearLights();
#endif

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

	XMFLOAT4 m_constantBuffer[53];
	UINT8* m_constantBufferGPUAddress[DX12_FRAME_COUNT];
	ID3D12PipelineState* m_activePipelineState = nullptr;
	UINT m_stencilRef = 0;

	UINT m_objectIndex = 0;

	// Synchronization
	UINT m_frameIndex;
	DX12Rendering::Fence m_frameFence;
	DX12Rendering::Fence m_copyFence;

	// Textures
	ComPtr<ID3D12Resource> m_textureBufferUploadHeap; // Intermediate texture upload space.
	UINT64 m_textureBufferUploadIndex = 0; // Intermediate offset to the texture upload.
	UINT64 m_textureBufferUploadMax = 0; // The max space for uploading textures.

	UINT8 m_activeTextureRegister;
	DX12TextureBuffer* m_activeTextures[TEXTURE_REGISTER_COUNT];

	// Device removal
	HANDLE m_deviceRemovedHandle;
	HANDLE m_removeDeviceEvent;

	// Raytracing
	DX12Rendering::Raytracing* m_raytracing;
	DX12Rendering::dx12_lock m_raytracingLock;

	void LoadPipeline(HWND hWnd);
	void LoadAssets();

	void SignalNextFrame();
    void WaitForPreviousFrame();
	void WaitForCopyToComplete();

	/// <summary>
	/// Copies a resource to the display buffer.
	/// </summary>
	/// <param name="resource"></param>
	/// <param name="sx">The source x location</param>
	/// <param name="sy">The source y location</param>
	/// <param name="rx">The result x location</param>
	/// <param name="ry">The result y location</param>
	/// <param name="width">The width of pixels to copy</param>
	/// <param name="height">The height of the pixels to copy</param>
	/// <param name="sourceState">The initial state of the source resource to create resource barriers</param>
	void CopyResourceToDisplay(ID3D12Resource* resource, UINT sx, UINT sy, UINT rx, UINT ry, UINT width, UINT height, D3D12_RESOURCE_STATES sourceState);

	bool CreateBackBuffer();

	bool IsScissorWindowValid();

#ifdef _DEBUG
	std::vector<viewLight_t> m_debugLights;

#ifdef DEBUG_IMGUI
	enum e_debugmode_t
	{
		DEBUG_UNKNOWN = 0,
		DEBUG_LIGHTS,
		DEBUG_RAYTRACING
	};

	ComPtr<ID3D12DescriptorHeap> m_imguiSrvDescHeap;
	e_debugmode_t m_debugMode;

	void InitializeImGui(HWND hWnd);
	void ReleaseImGui();
	void ImGuiDebugWindows();
#endif

#endif
};

extern DX12Renderer dxRenderer;
#endif