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
	typedef
		enum DXR_LIGHT_TYPE
	{
		DXR_LIGHT_TYPE_POINT = 1 << 0,
		DXR_LIGHT_TYPE_AMBIENT = 1 << 1,
		DXR_LIGHT_TYPE_FOG = 1 << 2
	} 	DXR_LIGHT_TYPE;

	DEFINE_ENUM_FLAG_OPERATORS(DXR_LIGHT_TYPE);

	// Making these caches 256x more than their in frame size
	const UINT VERTCACHE_INDEX_MEMORY = 31U * 1024U * 1024U * 256U;
	const UINT VERTCACHE_VERTEX_MEMORY = 31U * 1024U * 1024U * 256U;
	const UINT VERTCACHE_JOINT_MEMORY= 256U * 1024U * 256U;

	const UINT DESCRIPTOR_HEAP_SIZE = 9 /* basic entries */;
	const UINT DESCRIPTOR_OBJECT_TOTAL = MAX_SCENE_LIGHTS + 2;
	const UINT DESCRIPTOR_OBJECT_TOTAL_FRAMES = DX12_FRAME_COUNT * DESCRIPTOR_OBJECT_TOTAL;
	const UINT DESCRIPTOR_HEAP_TOTAL = DESCRIPTOR_HEAP_SIZE * DESCRIPTOR_OBJECT_TOTAL;

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
				bool isPointLight : 1;
				bool isAmbientLight : 1;
				bool isFogLight : 1;
				bool flagPad[sizeof(UINT) - 2];
			} flagValue;
		};
		UINT pad1;
		UINT pad2;
		float emissiveRadius; // Radius used to calculate soft shadows.

		XMFLOAT4 color;

		XMFLOAT4	center;

		XMFLOAT4	scissor; // Light view scissor window {left, top, right, bottom}

		// Used to calculate the angle and falloff for the light influence.
		XMFLOAT4	projectionS;
		XMFLOAT4	projectionT;
		XMFLOAT4	projectionQ;
		XMFLOAT4	falloffS;
	};

	struct dxr_global_illumination_t
	{
		UINT xDelta;
		UINT yDelta;
		UINT maxBounce;
		UINT sampleCount; // Total sample rays per hit point (this * max bounce gets your total number of rays)

		dxr_lightData_t lights[MAX_SCENE_LIGHTS];

		dxr_global_illumination_t(UINT xDelta, UINT yDelta, UINT maxBounce, UINT sampleCount, dxr_lightData_t* lightsIn, UINT lightCount) :
			xDelta(xDelta),
			yDelta(yDelta),
			maxBounce(maxBounce),
			sampleCount(sampleCount)
		{
			std::memcpy(lights, lightsIn, sizeof(dxr_lightData_t) * lightCount);
		};
	};

	struct dxr_sceneConstants_t
	{
		XMFLOAT4 renderParameters[DX12Rendering::dxr_renderParm_t::COUNT];

		UINT lightCount; // Note: This is no longer needed and can be changed if we would like.
		UINT diffuseTextureIndex;
		UINT specularTextureIndex;
		UINT materialTextureIndex;

		UINT positionTextureIndex;
		UINT flatNormalIndex;
		UINT normalIndex;
		UINT raysPerLight; // Number of shadow rays cast per light per pixel

		float noiseOffset;
		UINT pad0;
		UINT pad1;
		UINT pad2;
	};

	struct TopLevelAccelerationStructure;

	class Raytracing;
}



class DX12Rendering::Raytracing {
public:
	const bool isRaytracingSupported;

	Raytracing(UINT screenWidth, UINT screenHeight);
	~Raytracing();

	void Resize(UINT width, UINT height);

	DX12Rendering::BLASManager* GetBLASManager() { return &m_blasManager; }
	DX12Rendering::TLASManager* GetTLASManager() { return &m_tlasManager; }

	void Uniform4f(UINT index, const float* uniform);

	void ResetLightList();
	bool AddLight(const UINT index, const DXR_LIGHT_TYPE type, const DX12Rendering::TextureBuffer* falloffTexture, const DX12Rendering::TextureBuffer* projectionTexture, const UINT shadowMask, const XMFLOAT4& location, const XMFLOAT4& color, const XMFLOAT4* lightProjection, const XMFLOAT4& scissorWindow, bool castsShadows);
	UINT GetLightMask(const UINT index);

	void GenerateTLAS();
	void UpdateActiveBLAS();

	void CleanUpAccelerationStructure();

	void BeginFrame();
	void EndFrame();

	const DX12Rendering::Commands::FenceValue CastShadowRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect
	);

	const DX12Rendering::Commands::FenceValue CastGlobalIlluminationRays(
		const UINT frameIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect
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

	ComPtr<ID3D12StateObject> m_shadowStateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_shadowStateObjectProps;

	ComPtr<ID3D12StateObject> m_gliStateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_gliStateObjectProps;

	ComPtr<ID3D12RootSignature> m_globalRootSignature;

	dxr_sceneConstants_t m_constantBuffer;
	dxr_lightData_t m_activeLight; // Used to send the light to the shader
	dxr_lightData_t m_lights[MAX_SCENE_LIGHTS];

	TLASManager m_tlasManager;
	BLASManager m_blasManager;

	ComPtr<ID3D12DescriptorHeap> m_generalUavHeaps;
	ComPtr<ID3D12Resource> m_generalSBTData[DESCRIPTOR_OBJECT_TOTAL_FRAMES];
	UINT m_nextDescriptorHeapIndex;
	UINT m_cbvHeapIncrementor;

	ShaderBindingTable m_generalSBTDesc;

	UINT m_nextObjectIndex;

	// Pipeline
	void CreateShadowPipeline();
	void CreateGlobalIlluminationPipeline();
	void CreateOutputBuffers();
	void CreateShaderResourceHeap();

	void CreateShaderBindingTables();
	ID3D12Resource* CreateShadowBindingTable(UINT frameIndex, UINT objectIndex, ID3D12StateObjectProperties* props);

	void SetOutputTexture(DX12Rendering::eRenderSurface renderSurface, UINT frameIndex, UINT objectIndex, DX12Rendering::e_RaytracingHeapIndex uav);

	void CreateCBVHeap(const size_t constantBufferSize);
	D3D12_CONSTANT_BUFFER_VIEW_DESC SetCBVDescriptorTable(
		const size_t constantBufferSize, 
		const void* constantBuffer,
		const UINT frameIndex,
		const UINT objectIndex,
		const DX12Rendering::e_RaytracingHeapIndex heapIndex);

	void UpdateTlasDescriptor(UINT frameIndex, UINT objectIndex);
	void UpdateGeometryDescriptors(UINT frameIndex, UINT objectIndex);


	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(
		const UINT frameIndex,
		const UINT objectIndex,
		const DX12Rendering::e_RaytracingHeapIndex heapIndex);

	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		const UINT frameIndex,
		const UINT objectIndex);

	/// <summary>
	/// Cast rays into the scene through the general TLAS.
	/// </summary>
	/// <param name="commandList">The command list to use for casting.</param>
	/// <param name="viewport">The viewport size.</param>
	/// <param name="scissorRect">Any scissor rectable size.</param>
	/// <returns></returns>
	const void CastRays(
		const UINT frameIndex,
		const UINT objectIndex,
		const CD3DX12_VIEWPORT& viewport,
		const CD3DX12_RECT& scissorRect,
		DX12Rendering::RenderPassBlock& renderPass,
		ID3D12StateObject* pipelineState,
		ID3D12StateObjectProperties* stateProperties
	);

	// Acceleration Structure

	// Custom width and height selectors will be set based on definded scalers.
	UINT GetShadowWidth() const { return m_width; };
	UINT GetShadowHeight() const { return m_height;  };

	ID3D12RootSignature* GetGlobalRootSignature();

	ID3D12DescriptorHeap* GetUavHeap() { return m_generalUavHeaps.Get(); }

	const UINT RequestNewObjectIndex() {
		UINT result = m_nextObjectIndex;

		if ((++m_nextObjectIndex) >= DESCRIPTOR_OBJECT_TOTAL) { m_nextObjectIndex = 0; }

		return result;
	}
};
#endif