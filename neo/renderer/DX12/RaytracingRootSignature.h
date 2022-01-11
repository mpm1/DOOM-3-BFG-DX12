#ifndef __DX12_RAYTRACING_ROOT_SIGNATURE_H__
#define __DX12_RAYTRACING_ROOT_SIGNATURE_H__

namespace DX12Rendering {
	#include "./dx12_global.h"

	using namespace DirectX;

	enum eRootSignatureFlags {
		NONE = 0,
		WRITE_OUTPUT = 1,
		READ_ENVIRONMENT = 2
	};

	class RaytracingRootSignature;
}

class DX12Rendering::RaytracingRootSignature {
public:
	RaytracingRootSignature(ID3D12Device5* device, UINT flags);
	~RaytracingRootSignature();

	ID3D12RootSignature* GetRootSignature() { return m_rootSignature.Get(); }

private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	void CreateRootSignature(D3D12_ROOT_PARAMETER1* parameters, UINT parameterCount);
};

#endif