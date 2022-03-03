#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"
#include "./RaytracingRootSignature.h"
#include "./dx12_RaytracingPipeline.h"
#include "./dx12_ShaderBindingTable.h"

#define BIT_RAYTRACED_NONE			0x00000000
#define BIT_RAYTRACED_SHADOWS		0x00000001
#define BIT_RAYTRACED_REFLECTIONS	0x00000002
#define BIT_RAYTRACED_ILLUMINATION	0x00000004

#define DEFAULT_SCRATCH_SIZE 262144 // 256 * 1024. We need to check if this is big enough.

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering {
	struct Instance {
		ID3D12Resource* bottomLevelAS;
		XMMATRIX		transformation;
		UINT			instanceId;
		UINT			hitGroupIndex; // Should this be stage index?
		//TODO: Add support for bone information.

		Instance(ID3D12Resource* blas) : 
			bottomLevelAS(blas),
			transformation(),
			instanceId(0),
			hitGroupIndex(0) {}
	};

	class TopLevelAccelerationStructure;

	class Raytracing;
}

class DX12Rendering::TopLevelAccelerationStructure {
public:
	TopLevelAccelerationStructure(ID3D12Device5* device);
	~TopLevelAccelerationStructure();

	void AddInstance(DX12Object* object, DX12Stage* stage);
	void Reset(); // Clears the acceleration structure.
	void UpdateResources(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() { return m_result->GetGPUVirtualAddress(); }
private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12Resource> m_result; // Top Level Acceleration Structure - Used for raytracing.
	ComPtr<ID3D12Resource> m_instanceDesc;
	std::vector<DX12Rendering::Instance> m_instances;

	void CacluateBufferSizes(UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize);
};

class DX12Rendering::Raytracing {
public:
	const bool isRaytracingSupported;

	DX12Rendering::TopLevelAccelerationStructure shadowTlas; // Contains blocking objects used to calculate shadows.
	DX12Rendering::TopLevelAccelerationStructure reflectionTlas; // Contains all objects that would appear in reflections. We may be able to use this for global illumination.
	DX12Rendering::TopLevelAccelerationStructure emmisiveTlas; // Contains all objects emitting light.

	Raytracing(ID3D12Device5* device, UINT screenWidth, UINT screenHeight);
	~Raytracing();

	void Resize(UINT width, UINT height);

	void StartAccelerationStructure(bool raytracedShadows, bool raytracedReflections, bool raytracedIllumination);
	void EndAccelerationStructure(ID3D12GraphicsCommandList4* commandList);

	/// <summary>
	/// Cast the rays to the stencil buffer to store for shadow generation.
	/// </summary>
	/// <param name="commandList"></param>
	/// <param name="viewport"></param>
	/// <param name="scissorRect"></param>
	void CastShadowRays(ID3D12GraphicsCommandList4* commandList, 
		const CD3DX12_VIEWPORT& viewport, 
		const CD3DX12_RECT& scissorRect,
		ID3D12Resource* depthStencilBuffer,
		UINT32 stencilIndex);

	/// <summary>
	/// Generates the bottom level accelr
	/// </summary>
	/// <param name="commandList">The command list to generate the BLAS.</param>
	/// <param name="storedObject">The game object to obtain the needed data for the BLAS object.
	/// <param name="buffer">The resulting BLAS buffer resources.</param>
	/// <param name="updateOnly">If true, refit the existing BLAS.</param>
	void GenerateBottomLevelAS(
		ID3D12GraphicsCommandList4* commandList,
		DX12Object* storedObject,
		bool updateOnly);

	/// <summary>
	/// Adds the desired object to the various top level acceleration structures.
	/// </summary>
	/// <param name="storedObject">The object to grab all of the stages from.</param>
	/// <param name="updateOnly">Whether or not this is just an update to an existing object.</param>
	void AddObjectToAllTopLevelAS(
		DX12Object* storedObject,
		bool updateOnly); //TODO: Add matrix and bone information.
private:
	UINT32 m_state;
	ID3D12Device5* m_device;
	UINT m_width;
	UINT m_height;

	ComPtr<ID3D12Resource> m_scratchBuffer; // For now we will use the same scratch buffer for all AS creations.

	DX12Rendering::RaytracingRootSignature m_rayGenSignature;
	DX12Rendering::RaytracingRootSignature m_missSignature;
	DX12Rendering::RaytracingRootSignature m_hitSignature;

	ComPtr<ID3D12StateObject> m_shadowStateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_shadowStateObjectProps;

	ComPtr<ID3D12RootSignature> m_globalRootSignature;

	ComPtr<ID3D12Resource> m_shadowResource; // For testing right now, we will acutally copy to the stencil buffer when needed.
	ComPtr<ID3D12DescriptorHeap> m_shadowUavHeaps;
	ComPtr<ID3D12Resource> m_shadowSBTData;

	ShaderBindingTable m_shadowSBTDesc;

	// Pipeline
	void CreateShadowPipeline();
	void CreateOutputBuffers();
	void CreateShaderResourceHeap();

	void CreateShaderBindingTables();
	void CreateShadowBindingTable();

	// Acceleration Structure
	bool UpdateBLASResources(DX12Object* storedObject, bool updateOnly);
	void CacluateBLASBufferSizes(DX12Object* storedObject, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes);

	bool IsReflectiveEnabled() const;
	bool IsShadowEnabled() const;
	bool IsIlluminationEnabled() const;

	// Custom width and height selectors will be set based on definded scalers.
	UINT GetShadowWidth() const { return m_width; };
	UINT GetShadowHeight() const { return m_height;  };

	ID3D12RootSignature* GetGlobalRootSignature();
};
#endif