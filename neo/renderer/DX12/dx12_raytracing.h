#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"

using namespace DirectX;
using namespace Microsoft::WRL;

#define DEFAULT_SCRATCH_SIZE 262144 // 256 * 1024. We need to check if this is big enough.

struct DX12AccelerationStructureBuffers
{
	ComPtr<ID3D12Resource> pScratch; // Scratch memory for the acceleration structure builder.
	ComPtr<ID3D12Resource> pResult; // The location of the acceleration structure.
	ComPtr<ID3D12Resource> pInstanceDesc; // Holds the matrix data. TODO: It may be better to change this to point to the CBV Heap.
};

class DX12Raytracing {
public:
	const bool isRaytracingSupported;

	DX12Raytracing(ID3D12Device5* device);
	~DX12Raytracing();
	
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
	
	// Acceleration Structure
	void UpdateBLASResources(DX12Object* storedObject, bool updateOnly);
	void CacluateBLASBufferSizes(DX12Object* storedObject, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes);

	static bool CheckRaytracingSupport(ID3D12Device5* device);

	ComPtr<ID3D12Resource> scratchBuffer; // For now we will use the same scratch buffer for all AS creations.
};

#endif