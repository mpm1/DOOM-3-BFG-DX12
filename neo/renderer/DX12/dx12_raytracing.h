#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"
#include "./RaytracingRootSignature.h"

#define BIT_RAYTRACED_NONE			0x00000000
#define BIT_RAYTRACED_SHADOWS		0x00000001
#define BIT_RAYTRACED_REFLECTIONS	0x00000002
#define BIT_RAYTRACED_ILLUMINATION	0x00000004

#define DEFAULT_SCRATCH_SIZE 262144 // 256 * 1024. We need to check if this is big enough.

namespace DX12Rendering {
	using namespace DirectX;
	using namespace Microsoft::WRL;

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

	struct RootSignatureAssociation {
		ID3D12RootSignature* m_rootSignature;
		std::vector<std::wstring> m_symbols;

		RootSignatureAssociation(ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols) :
			m_rootSignature(rootSignature),
			m_symbols(symbols) {}
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

	Raytracing(ID3D12Device5* device);
	~Raytracing();

	void StartAccelerationStructure(bool raytracedShadows, bool raytracedReflections, bool raytracedIllumination);
	void EndAccelerationStructure(ID3D12GraphicsCommandList4* commandList);

	void CastRays(const CD3DX12_VIEWPORT viewport, const CD3DX12_RECT scissorRect) const;

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
	ComPtr<ID3D12Resource> m_scratchBuffer; // For now we will use the same scratch buffer for all AS creations.

	DX12Rendering::RaytracingRootSignature m_rayGenSignature;
	DX12Rendering::RaytracingRootSignature m_missSignature;
	DX12Rendering::RaytracingRootSignature m_hitSignature;

	ComPtr<ID3D12StateObject> m_stateObject; // Raytracing pipeline state.
	ComPtr<ID3D12StateObjectProperties> m_stateObjectProps;

	// Pipeline
	void CreatePipeline();
	void LoadShader(); // TODO: Implement function to load in the correct shader.

	// Acceleration Structure
	bool UpdateBLASResources(DX12Object* storedObject, bool updateOnly);
	void CacluateBLASBufferSizes(DX12Object* storedObject, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes);

	bool IsReflectiveEnabled() const;
	bool IsShadowEnabled() const;
	bool IsIlluminationEnabled() const;
};
#endif