#ifndef __DX12_RAYTRACING_ROOT_SIGNATURE_H__
#define __DX12_RAYTRACING_ROOT_SIGNATURE_H__

#include "./dx12_global.h"

namespace DX12Rendering {
	enum eRootSignatureFlags {
		NONE = 0,
		WRITE_OUTPUT = 1,
		READ_ENVIRONMENT = 2,
		ALL = WRITE_OUTPUT | READ_ENVIRONMENT
	};

	class RaytracingRootSignature;
}

class DX12Rendering::RaytracingRootSignature {
public:
	RaytracingRootSignature(UINT flags);
	~RaytracingRootSignature();

	ID3D12RootSignature* GetRootSignature() { return m_rootSignature.Get(); }

private:
	ComPtr<ID3D12RootSignature> m_rootSignature;

#if defined(_DEBUG)
	D3D12_ROOT_PARAMETER1 m_parameters[4];
	int m_parameterCount = 0;
#endif

	void CreateRootSignature(D3D12_ROOT_PARAMETER1* parameters, UINT parameterCount);
};

#endif