#ifndef __DX12_RAYTRACING_H__
#define __DX12_RAYTRACING_H__

#include "./dx12_global.h"

using namespace DirectX;
using namespace Microsoft::WRL;

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

private:
	ID3D12Device5* m_device;
	
	// Acceleration Structure


	static bool CheckRaytracingSupport(ID3D12Device5* device);
};

#endif