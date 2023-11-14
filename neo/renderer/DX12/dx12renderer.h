#ifndef __DX12_RENDERER_H__
#define __DX12_RENDERER_H__

#include <map>
#include <functional>
#include <mutex>

#include "./dx12_global.h"
#include "./dx12_DeviceManager.h"
#include "./dx12_CommandList.h"
#include "./dx12_RootSignature.h"
#include "./dx12_raytracing.h"
#include "./dx12_TextureManager.h"

// Use D3D clip space.
#define CLIP_SPACE_D3D

#define BUFFER_RGB 0x01
#define BUFFER_STENCIL 0x02

#define MAX_RENDER_TARGETS 5

#define COMMAND_LIST_COUNT 5

using namespace DirectX;
using namespace Microsoft::WRL;

class idRenderModel;

#ifdef _DEBUG
struct viewLight_t;
#endif

namespace DX12Rendering {

	enum eSurfaceVariant
	{
		VARIANT_DEFAULT = 0,
		VARIANT_STENCIL_SHADOW_RENDER_ZPASS,
		VARIANT_STENCIL_SHADOW_RENDER_ZPASS_SKINNED,
		VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD,
		VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD_SKINNED,
		VARIANT_STENCIL_TWOSIDED,

		VARIANT_COUNT
	};

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

	// Defines a single graphics pass (i.e. z-pass, GBuffer, transparents, emmisives). It will setup the root signature and all render targets for the pass.
	struct RenderPassBlock;
}

//TODO: move everything into the correct namespace
bool DX12_ActivatePipelineState(const DX12Rendering::eSurfaceVariant variant);

class DX12Renderer {
public:
	DX12Renderer();
	~DX12Renderer();

	void Init(HWND hWnd);
	bool SetScreenParams(UINT width, UINT height, int fullscreen);
	void OnDestroy();

	void UpdateViewport(const FLOAT topLeftX, const FLOAT topLeftY, const FLOAT width, const FLOAT height, const FLOAT minDepth = 0.0f, const FLOAT maxDepth = 1.0f); // Used to put us into right hand depth space.
	void UpdateScissorRect(const LONG x, const LONG y, const LONG w, const LONG h);
	void UpdateDepthBounds(const FLOAT minDepth, const FLOAT maxDepth);
	void UpdateStencilRef(UINT ref);

	void ReadPixels(int x, int y, int width, int height, UINT readBuffer, byte* buffer);

	// Shaders
	void LoadPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC* psoDesc, ID3D12PipelineState** ppPipelineState);
	void SetActivePipelineState(ID3D12PipelineState* pipelineState);

	void Uniform4f(UINT index, const float* uniform);

	// Buffers
	DX12Rendering::Geometry::VertexBuffer* AllocVertexBuffer(UINT numBytes, LPCWSTR name);
	void FreeVertexBuffer(DX12Rendering::Geometry::VertexBuffer* buffer);

	DX12Rendering::Geometry::IndexBuffer* AllocIndexBuffer(UINT numBytes, LPCWSTR name);
	void FreeIndexBuffer(DX12Rendering::Geometry::IndexBuffer* buffer);

	DX12Rendering::Geometry::JointBuffer* AllocJointBuffer(UINT numBytes);
	void FreeJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer);
	void SetJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer, UINT jointOffset);

	// Lights
	UINT SetLightData(const UINT sceneIndex, const UINT shadowMask);
	void MoveLightsToHeap();
	const DX12Rendering::ShaderLightData SetActiveLight(const UINT lightIndex);

	// Textures
	DX12Rendering::TextureManager* GetTextureManager() { return &m_textureManager; }
	void SetActiveTextureRegister(UINT8 index);
	void SetTexture(DX12Rendering::TextureBuffer* buffer);

	// Draw commands
	void BeginDraw();
	void Clear(const bool color, const bool depth, bool stencil, byte stencilValue, const float colorRGBA[4]);
	void EndDraw();
	void PresentBackbuffer();
	void SetCommandListDefaults(const bool resetPipelineState = true);
	void CycleDirectCommandList();
	UINT StartSurfaceSettings(); // Starts a new heap entry for the surface.
	bool EndSurfaceSettings(const DX12Rendering::eSurfaceVariant variant); // Records the the surface entry into the heap.
	void DrawModel(DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffset, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount, size_t vertexStrideOverride /* 0 means no override */);

#pragma region RayTracing
	void DXR_ResetAccelerationStructure(); // Resets the acceleration structure to an empty state.
	void DXR_UpdateAccelerationStructure();

	void DXR_UpdateModelInBLAS(const idRenderModel* model);

	void DXR_AddEntityToTLAS(const uint entityIndex, const idRenderModel& model, const float transform[16], const DX12Rendering::ACCELERATION_INSTANCE_TYPE typesMask);

	void DXR_SetRenderParam(DX12Rendering::dxr_renderParm_t param, const float* uniform);
	void DXR_SetRenderParams(DX12Rendering::dxr_renderParm_t param, const float* uniform, const UINT count);

	bool DXR_CastRays(); // Sets the appropriate matricies and casts the rays to the scene.

	void DXR_SetupLights(const viewLight_t* viewLights, const float* worldMatrix); // Adds all lights and orders them for rendering.

	void DXR_DenoiseResult(); // Performs a Denoise pass on all rendering channels.
	void DXR_GenerateResult(); // Collapses all channels into a single image.

	// Render Targets
	void SetRenderTargets(const DX12Rendering::eRenderSurface* surfaces, const UINT count);
	void EnforceRenderTargets(DX12Rendering::Commands::CommandList* commandList);
	void ResetRenderTargets();
	DX12Rendering::eRenderSurface GetOutputSurface() { return (DX12Rendering::eRenderSurface)(DX12Rendering::eRenderSurface::RenderTarget1 + m_frameIndex); }

#pragma region Top Level Acceleration Structure
	//TODO
#pragma endregion

	bool IsRaytracingEnabled() const { return m_raytracing != nullptr && m_raytracing->isRaytracingSupported; };
#pragma endregion

	// Converters
	template<typename T>
	const dxHandle_t GetHandle(const T* qEntity);

#ifdef _DEBUG
	void DebugAddLight(const viewLight_t& light);
	void DebugClearLights();

	void CopyDebugResultToDisplay(); // Copies the resulting image to the user display.
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
	ComPtr<IDXGISwapChain3> m_swapChain;
	DX12RootSignature* m_rootSignature;

	DX12Rendering::ShaderLightData m_lights[MAX_SCENE_LIGHTS]; // All lights being rendered into the scene.
	UINT m_activeLight = 0;

	XMFLOAT4 m_constantBuffer[57/* RENDERPARM_TOTAL */];
	UINT8* m_constantBufferGPUAddress[DX12_FRAME_COUNT];
	ID3D12PipelineState* m_activePipelineState = nullptr;
	UINT m_stencilRef = 0;

	UINT m_objectIndex = 0;

	// Render Targets
	UINT m_activeRenderTargets = 0;
	DX12Rendering::RenderSurface* m_renderTargets[MAX_RENDER_TARGETS];

	// Synchronization
	UINT m_frameIndex;
	DX12Rendering::Fence m_frameFence;
	DX12Rendering::Fence m_copyFence;

	// Textures
	DX12Rendering::TextureManager m_textureManager;
	UINT8 m_activeTextureRegister;
	DX12Rendering::TextureBuffer* m_activeTextures[TEXTURE_REGISTER_COUNT];

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
	/// Copies the contents of a render target to the display buffer.
	/// </summary>
	/// <param name="renderTarget"></param>
	/// <param name="sx">The source x location</param>
	/// <param name="sy">The source y location</param>
	/// <param name="rx">The result x location</param>
	/// <param name="ry">The result y location</param>
	/// <param name="width">The width of pixels to copy</param>
	/// <param name="height">The height of the pixels to copy</param>
	void CopySurfaceToDisplay(DX12Rendering::eRenderSurface surfaceId, UINT sx, UINT sy, UINT rx, UINT ry, UINT width, UINT height);

	bool CreateBackBuffer();

	bool IsScissorWindowValid();


	const DX12Rendering::RenderSurface** GetCurrentRenderTargets(UINT& count);
	DX12Rendering::RenderSurface* GetOutputRenderTarget() { return DX12Rendering::GetSurface(GetOutputSurface()); }

#ifdef _DEBUG
	std::vector<viewLight_t> m_debugLights;

#ifdef DEBUG_IMGUI
	enum e_debugmode_t
	{
		DEBUG_UNKNOWN = 0,
		DEBUG_LIGHTS,
		DEBUG_RAYTRACING,
		DEBUG_RAYTRACING_SHADOWMAP,
	};

	ComPtr<ID3D12DescriptorHeap> m_imguiSrvDescHeap;
	e_debugmode_t m_debugMode;

	void InitializeImGui(HWND hWnd);
	void ReleaseImGui();
	void ImGuiDebugWindows();
#endif

#endif
};

struct DX12Rendering::RenderPassBlock
{ // TODO: Make all passes use this block object.
	const UINT renderTargetCount;
	const std::string name;

	/// <summary>
	/// Defines a code block that will setup the current root signature and render targets. When the block is complete, we will execute the command lists and return to the generic render targets.
	/// </summary>
	/// <param name="name">Name of the block. This will show up in PIX captures.</param>
	/// <param name="commandListType">The type of command list to execute these actions on.</param>
	/// <param name="renderTargetList">An array of data specifying all of the render targets to attach.</param>
	/// <param name="renderTargetCount">The total numer of render targets to attach.</param>
	RenderPassBlock(const std::string name, const DX12Rendering::Commands::dx12_commandList_t commandListType, const DX12Rendering::eRenderSurface* renderTargetList = nullptr, const UINT renderTargetCount = 0);
	~RenderPassBlock();

	DX12Rendering::Commands::CommandList* GetCommandList() { return m_commandList; }
	static RenderPassBlock* GetCurrentRenderPass();

private:
	DX12Rendering::eRenderSurface m_renderSurfaces[MAX_RENDER_TARGETS];
	DX12Rendering::Commands::CommandList* m_commandList;

	void UpdateRenderState(D3D12_RESOURCE_STATES renderState);
};

extern DX12Renderer dxRenderer;
#endif