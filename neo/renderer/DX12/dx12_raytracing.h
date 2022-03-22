#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"
#include "./RaytracingRootSignature.h"
#include "./dx12_RaytracingPipeline.h"
#include "./dx12_ShaderBindingTable.h"
#include "./dx12_AccelerationStructure.h"

#define BIT_RAYTRACED_NONE			0x00000000
#define BIT_RAYTRACED_SHADOWS		0x00000001
#define BIT_RAYTRACED_REFLECTIONS	0x00000002
#define BIT_RAYTRACED_ILLUMINATION	0x00000004

#define DEFAULT_SCRATCH_SIZE 262144 // 256 * 1024. We need to check if this is big enough.

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering {
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

	// TODO: Possibly remove these.
	//void StartAccelerationStructure(bool raytracedShadows, bool raytracedReflections, bool raytracedIllumination);
	//void EndAccelerationStructure();

	DX12Rendering::TopLevelAccelerationStructure* GetShadowTLAS(const dxHandle_t& handle);
	DX12Rendering::TopLevelAccelerationStructure* EmplaceShadowTLAS(const dxHandle_t& handle);
	void GenerateTLAS(DX12Rendering::TopLevelAccelerationStructure* tlas);

	/// <summary>
	/// Cast the rays to the stencil buffer to store for shadow generation.
	/// </summary>
	/// <param name="commandList"></param>
	/// <param name="viewport"></param>
	/// <param name="scissorRect"></param>
	void CastShadowRays(
		ID3D12GraphicsCommandList4* commandList,
		const dxHandle_t lightHandle,
		const CD3DX12_VIEWPORT& viewport, 
		const CD3DX12_RECT& scissorRect,
		ID3D12Resource* depthStencilBuffer,
		UINT32 stencilIndex);

	/// <summary>
	/// Adds the desired object to the various top level acceleration structures.
	/// </summary>
	void AddObjectToAllTopLevelAS(); //TODO: Add matrix and bone information.
private:
	UINT32 m_state;
	ID3D12Device5* m_device;
	UINT m_width;
	UINT m_height;

	DX12Rendering::RaytracingRootSignature m_rayGenSignature;
	DX12Rendering::RaytracingRootSignature m_missSignature;
	DX12Rendering::RaytracingRootSignature m_hitSignature;

	ComPtr<ID3D12StateObject> m_shadowStateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_shadowStateObjectProps;

	ComPtr<ID3D12RootSignature> m_globalRootSignature;

	std::map<const dxHandle_t, TopLevelAccelerationStructure> m_shadowTlas = {};
	ComPtr<ID3D12Resource> m_shadowResource; // For testing right now, we will acutally copy to the stencil buffer when needed.
	ComPtr<ID3D12DescriptorHeap> m_shadowUavHeaps;
	ComPtr<ID3D12Resource> m_shadowSBTData;

	ShaderBindingTable m_shadowSBTDesc;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;

	// Pipeline
	void CreateShadowPipeline();
	void CreateOutputBuffers();
	void CreateShaderResourceHeap();

	void CreateShaderBindingTables();
	void CreateShadowBindingTable();

	// Command List
	void CreateCommandList();
	void ResetCommandAllocator();

	// Acceleration Structure

	bool IsReflectiveEnabled() const;
	bool IsShadowEnabled() const;
	bool IsIlluminationEnabled() const;

	// Custom width and height selectors will be set based on definded scalers.
	UINT GetShadowWidth() const { return m_width; };
	UINT GetShadowHeight() const { return m_height;  };

	ID3D12RootSignature* GetGlobalRootSignature();
};
#endif