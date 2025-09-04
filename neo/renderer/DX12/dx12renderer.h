#ifndef __DX12_RENDERER_H__
#define __DX12_RENDERER_H__

#include <map>
#include <functional>
#include <mutex>

#include "./dx12_global.h"
#include "./dx12_DeviceManager.h"
#include "./dx12_CommandList.h"
#include "./dx12_RenderPass.h"
#include "./dx12_RootSignature.h"
#include "./dx12_raytracing.h"
#include "./dx12_TextureManager.h"
#include "./dx12_shader.h"

// Use D3D clip space.
#define CLIP_SPACE_D3D

#define BUFFER_RGB 0x01
#define BUFFER_STENCIL 0x02

#define COMMAND_LIST_COUNT 5

using namespace DirectX;
using namespace Microsoft::WRL;

class idRenderModel;

struct viewLight_t;

constexpr int DYNAMIC_VERTEX_MEMORY_PER_FRAME = 31 * 1024 * 1024; // Matches VERTCACHE_VERTEX_MEMORY_PER_FRAME
constexpr int DYNAMIC_VERTEX_ALIGNMENT = 32; // Matches VERTEX_CACHE_ALIGN

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

	struct TextureConstants
	{
		UINT textureIndex[8];
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
}

//TODO: move everything into the correct namespace
bool DX12_ActivatePipelineState(const DX12Rendering::eSurfaceVariant variant, const idMaterial* material, DX12Rendering::Commands::CommandList& commandList);

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
	void SetActivePipelineState(ID3D12PipelineState* pipelineState, DX12Rendering::Commands::CommandList& commandList);

	void Uniform4f(UINT index, const float* uniform);

	// Buffers
	DX12Rendering::Geometry::VertexBuffer* AllocVertexBuffer(const UINT numBytes, const LPCWSTR name, const bool isGPUWritable);
	void FreeVertexBuffer(DX12Rendering::Geometry::VertexBuffer* buffer);

	DX12Rendering::Geometry::IndexBuffer* AllocIndexBuffer(UINT numBytes, LPCWSTR name);
	void FreeIndexBuffer(DX12Rendering::Geometry::IndexBuffer* buffer);

	DX12Rendering::Geometry::JointBuffer* AllocJointBuffer(UINT numBytes);
	void FreeJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer);
	void SetJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer, UINT jointOffset, DX12Rendering::Commands::CommandList* commandList);

	void StartComputeSurfaceBones();
	void EndComputeSurfaceBones();
	UINT ComputeSurfaceBones(DX12Rendering::Geometry::VertexBuffer* srcBuffer, UINT inputOffset, UINT outputOffset, UINT vertBytes, DX12Rendering::Geometry::JointBuffer* joints, UINT jointOffset);

	// Textures
	void SetActiveTextureRegister(UINT8 index);
	void SetTexture(DX12Rendering::TextureBuffer* buffer);

	// Draw commands
	void BeginDraw(const int frameIndex);
	DX12Rendering::Commands::CommandList* Clear(const bool color, const bool depth, bool stencil, byte stencilValue, const float colorRGBA[4], DX12Rendering::Commands::CommandList* commandList);
	void EndDraw();
	void PresentBackbuffer();
	void SetPassDefaults(DX12Rendering::Commands::CommandList* commandList, const bool isComputeQueue);
	int StartSurfaceSettings(const DX12Rendering::eSurfaceVariant variantState, const idMaterial* material, DX12Rendering::Commands::CommandList& commandList); // Starts a new heap entry for the surface.
	bool EndSurfaceSettings(void* surfaceConstants, size_t surfaceConstantsSize, DX12Rendering::Commands::CommandList& commandList); // Records the the surface entry into the heap.
	void DrawModel(DX12Rendering::Commands::CommandList& commandList, DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffset, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount, size_t vertexStrideOverride /* 0 means no override */);

#pragma region RayTracing
	void DXR_ResetAccelerationStructure(); // Resets the acceleration structure to an empty state.
	void DXR_UpdateAccelerationStructure();

	bool DXR_BLASExists(const dxHandle_t id);
	dxHandle_t DXR_GetBLASHandle(const idRenderModel* model);
	DX12Rendering::BottomLevelAccelerationStructure* DXR_UpdateModelInBLAS(const idRenderModel* model);
	DX12Rendering::BottomLevelAccelerationStructure* DXR_UpdateBLAS(const dxHandle_t id, const char* name, const bool isStatic, const size_t surfaceCount, DX12Rendering::RaytracingGeometryArgument* arguments);
	UINT DXR_UpdatePendingBLAS();

	void DXR_RemoveModelInBLAS(const idRenderModel* model);
	void DXR_RemoveBLAS(const dxHandle_t id);

	void DXR_AddModelBLASToTLAS(const uint entityIndex, const idRenderModel& model, const float transform[16], const DX12Rendering::ACCELERATION_INSTANCE_TYPE typesMask, const DX12Rendering::ACCELLERATION_INSTANCE_MASK instanceMask);
	void DXR_AddBLASToTLAS(const uint entityIndex, const dxHandle_t id, const float transform[16], const DX12Rendering::ACCELERATION_INSTANCE_TYPE typesMask, const DX12Rendering::ACCELLERATION_INSTANCE_MASK instanceMask);

	void DXR_SetRenderParam(DX12Rendering::dxr_renderParm_t param, const float* uniform);
	void DXR_SetRenderParams(DX12Rendering::dxr_renderParm_t param, const float* uniform, const UINT count);

	bool DXR_CastRays(); // Sets the appropriate matricies and casts the rays to the scene.

	void DXR_SetupLights(const viewLight_t* viewLights, const float* worldMatrix); // Adds all lights and orders them for rendering.

	void DXR_DenoiseResult(); // Performs a Denoise pass on all rendering channels.

	// Render Targets
	void SetRenderTargets(const DX12Rendering::eRenderSurface* surfaces, const UINT count);
	void EnforceRenderTargets(DX12Rendering::Commands::CommandList* commandList);
	void ResetRenderTargets();
	DX12Rendering::eRenderSurface GetOutputSurface() { return (DX12Rendering::eRenderSurface)(DX12Rendering::eRenderSurface::RenderTarget1 + DX12Rendering::GetCurrentFrameIndex()); }

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
#endif

private:
	UINT m_width;
	UINT m_height;
	int m_fullScreen; // 0 = windowed, otherwise 1 based monitor number to go full screen on
						// -1 = borderless window for spanning multiple displays

	DX12Rendering::Commands::FenceValue m_endFrameFence;

	FLOAT m_aspectRatio = 1.0f;
    FLOAT m_FoV = 90.0f;
	FLOAT m_zMin = 0.0f; // Set's the min depth bounds
	FLOAT m_zMax = 1.0f; // Set's the max depth bounds

	bool m_isDrawing = false;
	bool m_initialized = false;

	// Pipeline
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	DX12Rendering::RenderRootSignature* m_rootSignature;
	DX12Rendering::ComputeRootSignature* m_computeRootSignature;

	XMFLOAT4 m_constantBuffer[58/* RENDERPARM_TOTAL */];
	UINT8* m_constantBufferGPUAddress[DX12_FRAME_COUNT];
	ID3D12PipelineState* m_activePipelineState = nullptr;
	UINT m_stencilRef = 0;

	UINT m_objectIndex = 0;

	// Compute Shaders
	ComPtr<ID3D12PipelineState> m_skinnedModelShader = nullptr;

	// Dynamic Surface Data
	UINT m_nextDynamicVertexIndex = 0;
	DX12Rendering::Geometry::VertexBuffer* m_dynamicVertexBuffer[DX12_FRAME_COUNT];

	// Render Targets
	UINT m_activeRenderTargets = 0;
	DX12Rendering::RenderSurface* m_renderTargets[MAX_RENDER_TARGETS];

	// Textures
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

	void CreateDynamicPerFrameData();
	void DestroyDynamicPerFrameData();
	void ResetDynamicPerFrameData();

	void SignalNextFrame();
    void WaitForPreviousFrame();

	bool CreateBackBuffer();

	bool IsScissorWindowValid();


	const DX12Rendering::RenderSurface** GetCurrentRenderTargets(UINT& count);
	DX12Rendering::RenderSurface* GetOutputRenderTarget() { return DX12Rendering::GetSurface(GetOutputSurface()); }
	inline DX12Rendering::Geometry::VertexBuffer* GetCurrentDynamicVertexBuffer() { return m_dynamicVertexBuffer[DX12Rendering::GetCurrentFrameIndex()]; }

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

extern DX12Renderer dxRenderer;
#endif