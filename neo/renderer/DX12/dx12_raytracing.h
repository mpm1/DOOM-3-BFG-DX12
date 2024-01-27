#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"
#include "./RaytracingRootSignature.h"
#include "./dx12_RaytracingPipeline.h"
#include "./dx12_ShaderBindingTable.h"
#include "./dx12_AccelerationStructure.h"
#include "./dx12_Geometry.h"
#include "./dx12_RenderTarget.h"

//#include "./dx12_buffermap.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering {
	// Making these caches 256x more than their in frame size
	const UINT VERTCACHE_INDEX_MEMORY = 31 * 1024 * 1024 * 256;
	const UINT VERTCACHE_VERTEX_MEMORY = 31 * 1024 * 1024 * 256;
	const UINT VERTCACHE_JOINT_MEMORY= 256 * 1024 * 256;

	const UINT DESCRIPTOR_TEXTURE_COUNT = 1024;
	const UINT DESCRIPTOR_HEAP_SIZE = 6 /* basic entries */ + DESCRIPTOR_TEXTURE_COUNT /* texture space */;

	enum dxr_renderParm_t {
		RENDERPARM_GLOBALEYEPOS = 0,
		RENDERPARM_FOV, // { min fov x, min fov y, max fov x, max fov y }
		RENDERPARM_VIEWPORT, // {left, top, right, bottom}
		RENDERPARAM_SCISSOR, // {left, top, right, bottom}

		RENDERPARM_INVERSE_VIEWMATRIX_X,
		RENDERPARM_INVERSE_VIEWMATRIX_Y,
		RENDERPARM_INVERSE_VIEWMATRIX_Z,
		RENDERPARM_INVERSE_VIEWMATRIX_W,

		RENDERPARM_INVERSE_PROJMATRIX_X,
		RENDERPARM_INVERSE_PROJMATRIX_Y,
		RENDERPARM_INVERSE_PROJMATRIX_Z,
		RENDERPARM_INVERSE_PROJMATRIX_W,

		COUNT
	};

	struct dxr_lightData_t
	{
		UINT lightIndex;
		UINT shadowMask;
		UINT falloffIndex;
		UINT projectionIndex;
		
		union
		{
			UINT flags;
			struct {
				bool castsShadows : 1;
				bool flagPad[sizeof(UINT) - 1];
			};
		};
		UINT pad1;
		UINT pad2;
		UINT pad3;

		XMFLOAT4 color;

		XMFLOAT4	center;

		XMFLOAT4	scissor; // Light view scissor window {left, top, right, bottom}

		// Used to calculate the angle and falloff for the light influence.
		XMFLOAT4	projectionS;
		XMFLOAT4	projectionT;
		XMFLOAT4	projectionQ;
		XMFLOAT4	falloffS;
	};

	struct dxr_sceneConstants_t
	{
		XMFLOAT4 renderParameters[DX12Rendering::dxr_renderParm_t::COUNT];

		UINT lightCount;
		UINT pad[3];

		dxr_lightData_t lights[MAX_SCENE_LIGHTS];
	};

	class TopLevelAccelerationStructure;

	class Raytracing;
}

class DX12Rendering::Raytracing {
public:
	const bool isRaytracingSupported;

	Raytracing(UINT screenWidth, UINT screenHeight);
	~Raytracing();

	void Resize(UINT width, UINT height, DX12Rendering::TextureManager &textureManager);

	DX12Rendering::BLASManager* GetBLASManager() { return &m_blasManager; }
	DX12Rendering::TLASManager* GetTLASManager() { return &m_tlasManager; }

	void Uniform4f(UINT index, const float* uniform);

	void ResetLightList();
	bool AddLight(const UINT index, const DX12Rendering::TextureBuffer* falloffTexture, const DX12Rendering::TextureBuffer* projectionTexture, const UINT shadowMask, const XMFLOAT4 location, XMFLOAT4 color, const XMFLOAT4 lightProjection[4], const XMFLOAT4 scissorWindow, bool castsShadows);
	UINT GetLightMask(const UINT index);

	/// Adds an image to the descriptor heap and returns the associated index.
	UINT AddImageToDescriptorHeap(const DX12Rendering::TextureBuffer* texture);

	void GenerateTLAS();

	void CleanUpAccelerationStructure();

	void BeginFrame();
	void EndFrame();

	bool CastShadowRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect,
		TextureManager* textureManager
	);

	/// <summary>
	/// Cast rays into the scene through the general TLAS.
	/// </summary>
	/// <param name="commandList">The command list to use for casting.</param>
	/// <param name="viewport">The viewport size.</param>
	/// <param name="scissorRect">Any scissor rectable size.</param>
	/// <returns></returns>
	bool CastRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect,
		const DX12Rendering::eRenderSurface* renderTargetList,
		const UINT renderTargetCount,
		TextureBuffer** buffers,
		const UINT bufferCount,
		DX12Rendering::TextureManager* textureManager
	);

	/// <summary>
	/// Adds the desired object to the various top level acceleration structures.
	/// </summary>
	void AddObjectToAllTopLevelAS(); //TODO: Add matrix and bone information.

	// Buffer updates
	//BufferEntity<DX12Rendering::Geometry::VertexBuffer>* AddOrUpdateVertecies(ID3D12Device5* device, qhandle_t entityHandle, byte* data, UINT size) { return m_localVertexBuffer.AddOrUpdateEntity(device, entityHandle, data, size); }
	//BufferEntity<DX12Rendering::Geometry::IndexBuffer>* AddOrUpdateIndecies(ID3D12Device5* device, qhandle_t entityHandle, byte* data, UINT size) { return m_localIndexBuffer.AddOrUpdateEntity(device, entityHandle, data, size); }

#ifdef DEBUG_IMGUI
	void ImGuiDebug();
#endif

private:
	UINT m_width;
	UINT m_height;

	//DX12Rendering::VertexBufferMap m_localVertexBuffer;
	//DX12Rendering::IndexBufferMap m_localIndexBuffer;

	DX12Rendering::RaytracingRootSignature m_rayGenSignature;
	DX12Rendering::RaytracingRootSignature m_missSignature;
	DX12Rendering::RaytracingRootSignature m_hitSignature;

	ComPtr<ID3D12Resource> m_cbvUploadHeap[DX12_FRAME_COUNT];
	UINT m_cbvHeapIncrementor;

	ComPtr<ID3D12StateObject> m_shadowStateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_shadowStateObjectProps;

	ComPtr<ID3D12RootSignature> m_globalRootSignature;

	dxr_sceneConstants_t m_constantBuffer;

	TLASManager m_tlasManager;
	BLASManager m_blasManager;

	ComPtr<ID3D12DescriptorHeap> m_generalUavHeaps;
	ComPtr<ID3D12Resource> m_generalSBTData;
	UINT m_nextDescriptorHeapIndex;

	ShaderBindingTable m_generalSBTDesc;

	// Pipeline
	void CreateShadowPipeline();
	void CreateOutputBuffers();
	void CreateShaderResourceHeap(DX12Rendering::TextureManager& textureManager);

	void CreateShaderBindingTables();
	void CreateShadowBindingTable();

	void SetOutputTexture(DX12Rendering::eRenderSurface renderSurface, DX12Rendering::e_RaytracingHeapIndex uav);

	void CreateCBVHeap(const size_t constantBufferSize);
	D3D12_CONSTANT_BUFFER_VIEW_DESC SetCBVDescriptorTable(
		const size_t constantBufferSize, 
		const void* constantBuffer,
		const UINT frameIndex);

	// Acceleration Structure

	// Custom width and height selectors will be set based on definded scalers.
	UINT GetShadowWidth() const { return m_width; };
	UINT GetShadowHeight() const { return m_height;  };

	ID3D12RootSignature* GetGlobalRootSignature();

	ID3D12DescriptorHeap* GetUavHeap() { return m_generalUavHeaps.Get(); }
};
#endif