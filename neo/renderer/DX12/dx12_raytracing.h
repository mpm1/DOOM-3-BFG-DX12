#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"

using namespace DirectX;
using namespace Microsoft::WRL;

#define DEFAULT_SCRATCH_SIZE 262144 // 256 * 1024. We need to check if this is big enough.

struct DX12Instance {
	ID3D12Resource*	bottomLevelAS;
	XMMATRIX		transformation;
	UINT			instanceId;
	UINT			hitGroupIndex; // Should this be stage index?
	//TODO: Add support for bone information.
};

class DX12Raytracing {
public:
	const bool isRaytracingSupported;
	const DX12TopLevelAccelerationStructure tlas;

	DX12Raytracing(ID3D12Device5* device);
	~DX12Raytracing();
	
	void StartAccelerationStructure();
	void EndAccelerationStructure();

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
private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12Resource> m_scratchBuffer; // For now we will use the same scratch buffer for all AS creations.

	// Acceleration Structure
	void UpdateBLASResources(DX12Object* storedObject, bool updateOnly);
	void CacluateBLASBufferSizes(DX12Object* storedObject, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes);
};


class DX12TopLevelAccelerationStructure {
public:
	DX12TopLevelAccelerationStructure(ID3D12Device5* device);
	~DX12TopLevelAccelerationStructure();

	void Reset(); // Clears the acceleration structure.
	DX12Instance* AddInstance(DX12Object* object, DX12Stage* stage);

private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12Resource> m_result; // Top Level Acceleration Structure - Used for raytracing.
	ComPtr<ID3D12Resource> m_instanceDesc;
	std::vector<DX12Instance> m_instances;

	void UpdateResources(DX12Object* storedObject, DX12Stage* stage);
	void CacluateBufferSizes(DX12Object* storedObject, DX12Stage* stage, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize);
};

#endif