#ifndef __DX12_RAYTRACING_PIPELINE_H__
#define __DX12_RAYTRACING_PIPELINE_H__

#include "./dx12_global.h"
#include "./dx12_shader.h"

namespace DX12Rendering {
	struct RootSignatureAssociation {
		ID3D12RootSignature* m_rootSignature;
		std::vector<LPCWSTR> m_symbols;
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION m_association = {};

		RootSignatureAssociation(ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols);
	};

	class RaytracingPipeline;
}

class DX12Rendering::RaytracingPipeline {
public:
	RaytracingPipeline(ID3D12Device5* device, const char* libraryName, ID3D12RootSignature* globalRootSignature, const std::vector<LPCWSTR>& symbolExports);
	~RaytracingPipeline();

	void AddHitGroup(const std::wstring& name, const std::wstring& closestHitSymbol); //NOTE: We only care about the closest hit right now.
	void AddAssocation(ID3D12RootSignature* m_rootSignature, const std::vector<std::wstring>& symbols);
	void SetMaxPayloadSize(const UINT maxPayloadSize) { m_maxPayloadSize = maxPayloadSize; }

	ID3D12StateObject* Generate();
private:
	ID3D12Device5* m_device;
	DX12Rendering::CompiledShader m_shader;
	D3D12_DXIL_LIBRARY_DESC m_libDesc;
	std::vector<D3D12_EXPORT_DESC> m_symbolDesc;
	std::vector<D3D12_HIT_GROUP_DESC> m_hitGroupDesc;
	std::vector<RootSignatureAssociation> m_associations;

	ID3D12RootSignature* m_globalRootSignature;
	ComPtr<ID3D12RootSignature> m_localRootSignature;

	UINT m_maxPayloadSize = 0;
	UINT m_maxAttributeSize = 2 * sizeof(float); // Will we only need this since were using the barycentric coordinates
	UINT m_maxRecursion = 2;
};

#endif