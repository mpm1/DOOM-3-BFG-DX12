#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"
#include "./RaytracingRootSignature.h"
#include "./dx12_RaytracingPipeline.h"
#include "./dx12_ShaderBindingTable.h"
#include "./dx12_AccelerationStructure.h"

#include "./dx12_buffermap.h"

#define DEFAULT_SCRATCH_SIZE 3145728 // 1024 * 1024 * 3. We need to check if this is big enough.

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

	ComPtr<ID3D12Resource> scratchBuffer; // For now we will use the same scratch buffer for all AS creations.

	DX12Rendering::BottomLevelAccelerationStructure blas; // Bottom level acceleration structer used to define our base instances.

	Raytracing(ID3D12Device5* device, UINT screenWidth, UINT screenHeight);
	~Raytracing();

	void Resize(UINT width, UINT height);

	/// <summary>
	/// Resets the command allocator. Should be called after a frame is completed.
	/// </summary>
	void ResetFrame();
	void ExecuteCommandList();
	void ResetCommandList();
	ID3D12GraphicsCommandList4* GetCommandList() const { return m_commandList.Get();  }

	void Uniform4f(dxr_renderParm_t param, const float* uniform);

	DX12Rendering::TopLevelAccelerationStructure* GetGeneralTLAS() { return &m_generalTlas; }
	void ResetGeneralTLAS();
	void GenerateTLAS(DX12Rendering::TopLevelAccelerationStructure* tlas);

	void CleanUpAccelerationStructure();

	void BeginFrame(){ GetGeneralTLAS()->IncrementFrameIndex(); }

	/// <summary>
	/// Cast rays into the scene through the general TLAS.
	/// </summary>
	/// <param name="commandList">The command list to use for casting.</param>
	/// <param name="viewport">The viewport size.</param>
	/// <param name="scissorRect">Any scissor rectable size.</param>
	/// <returns></returns>
	bool CastRays(
		ID3D12GraphicsCommandList4* commandList,
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
	ID3D12Device5* m_device;
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

	TopLevelAccelerationStructure m_generalTlas;
	ComPtr<ID3D12Resource> m_diffuseResource;
	ComPtr<ID3D12DescriptorHeap> m_generalUavHeaps;
	ComPtr<ID3D12Resource> m_generalSBTData;

	ShaderBindingTable m_generalSBTDesc;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;

	// Pipeline
	void CreateShadowPipeline();
	void CreateOutputBuffers();
	void CreateShaderResourceHeap();

	void CreateShaderBindingTables();
	void CreateShadowBindingTable();

	void CreateCBVHeap(const size_t constantBufferSize);
	D3D12_CONSTANT_BUFFER_VIEW_DESC SetCBVDescriptorTable(
		ID3D12GraphicsCommandList* commandList, 
		const size_t constantBufferSize, 
		const XMFLOAT4* constantBuffer, 
		const UINT frameIndex);

	// Command List
	void CreateCommandList();
	void ResetCommandAllocator();

	// Acceleration Structure

	// Custom width and height selectors will be set based on definded scalers.
	UINT GetShadowWidth() const { return m_width; };
	UINT GetShadowHeight() const { return m_height;  };

	ID3D12RootSignature* GetGlobalRootSignature();
};
#endif