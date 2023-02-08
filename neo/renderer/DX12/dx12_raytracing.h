#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"
#include "./RaytracingRootSignature.h"
#include "./dx12_RaytracingPipeline.h"
#include "./dx12_ShaderBindingTable.h"
#include "./dx12_AccelerationStructure.h"

#include "./dx12_buffermap.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering {
	// Making these caches 256x more than their in frame size
	const UINT VERTCACHE_INDEX_MEMORY = 31 * 1024 * 1024 * 256;
	const UINT VERTCACHE_VERTEX_MEMORY = 31 * 1024 * 1024 * 256;
	const UINT VERTCACHE_JOINT_MEMORY= 256 * 1024 * 256;

	enum dxr_renderParm_t {
		RENDERPARM_GLOBALEYEPOS = 0,
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

	class TopLevelAccelerationStructure;

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

	void Uniform4f(dxr_renderParm_t param, const float* uniform);

	void GenerateTLAS();

	void CleanUpAccelerationStructure();

	void BeginFrame();

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
		const CD3DX12_RECT& scissorRect
	);

	ID3D12Resource* GetOutputResource() { return m_diffuseResource.Get(); }

	/// <summary>
	/// Adds the desired object to the various top level acceleration structures.
	/// </summary>
	void AddObjectToAllTopLevelAS(); //TODO: Add matrix and bone information.

	// Buffer updates
	BufferEntity<DX12VertexBuffer>* AddOrUpdateVertecies(ID3D12Device5* device, qhandle_t entityHandle, byte* data, UINT size) { return m_localVertexBuffer.AddOrUpdateEntity(device, entityHandle, data, size); }
	BufferEntity<DX12IndexBuffer>* AddOrUpdateIndecies(ID3D12Device5* device, qhandle_t entityHandle, byte* data, UINT size) { return m_localIndexBuffer.AddOrUpdateEntity(device, entityHandle, data, size); }

#ifdef DEBUG_IMGUI
	void ImGuiDebug();
#endif

private:
	UINT m_width;
	UINT m_height;

	DX12Rendering::VertexBufferMap m_localVertexBuffer;
	DX12Rendering::IndexBufferMap m_localIndexBuffer;

	DX12Rendering::RaytracingRootSignature m_rayGenSignature;
	DX12Rendering::RaytracingRootSignature m_missSignature;
	DX12Rendering::RaytracingRootSignature m_hitSignature;

	ComPtr<ID3D12Resource> m_cbvUploadHeap[DX12_FRAME_COUNT];
	UINT m_cbvHeapIncrementor;

	ComPtr<ID3D12StateObject> m_shadowStateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_shadowStateObjectProps;

	ComPtr<ID3D12RootSignature> m_globalRootSignature;

	XMFLOAT4 m_constantBuffer[DX12Rendering::dxr_renderParm_t::COUNT];

	TLASManager m_tlasManager;
	BLASManager m_blasManager;

	ComPtr<ID3D12Resource> m_diffuseResource;
	ComPtr<ID3D12DescriptorHeap> m_generalUavHeaps;
	ComPtr<ID3D12Resource> m_generalSBTData;

	ShaderBindingTable m_generalSBTDesc;

	// Pipeline
	void CreateShadowPipeline();
	void CreateOutputBuffers();
	void CreateShaderResourceHeap();

	void CreateShaderBindingTables();
	void CreateShadowBindingTable();

	void CreateCBVHeap(const size_t constantBufferSize);
	D3D12_CONSTANT_BUFFER_VIEW_DESC SetCBVDescriptorTable(
		const size_t constantBufferSize, 
		const XMFLOAT4* constantBuffer, 
		const UINT frameIndex);

	// Acceleration Structure

	// Custom width and height selectors will be set based on definded scalers.
	UINT GetShadowWidth() const { return m_width; };
	UINT GetShadowHeight() const { return m_height;  };

	ID3D12RootSignature* GetGlobalRootSignature();
};
#endif