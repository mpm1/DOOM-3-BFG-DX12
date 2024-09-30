#ifndef __DX12_RAYTRACING_PIPELINE_H__
#define __DX12_RAYTRACING_PIPELINE_H__

#include "./dx12_global.h"
#include "./dx12_shader.h"

#include <unordered_set>;

namespace DX12Rendering {
	struct RootSignatureAssociation {
		ID3D12RootSignature* m_rootSignature;
		std::vector<LPCWSTR> m_symbols;
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION m_association = {};

		RootSignatureAssociation(ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols);
	};

	struct HitGroupDescription {
		std::wstring m_name;
		std::wstring m_closestHitSymbol;
		D3D12_HIT_GROUP_DESC m_hitGroupDesc = {};

		HitGroupDescription(std::wstring hitGroupName, std::wstring closestHitSymbol);
	};

	class RaytracingPipeline;
}

class DX12Rendering::RaytracingPipeline {
public:
	RaytracingPipeline(ID3D12Device5* device, ID3D12RootSignature* globalRootSignature);
	~RaytracingPipeline();

	void AddHitGroup(const std::wstring& name, const std::wstring& closestHitSymbol); //NOTE: We only care about the closest hit right now.
	void AddAssocation(ID3D12RootSignature* m_rootSignature, const std::vector<std::wstring>& symbols);
	void AddLibrary(const std::string libraryName, const std::vector<std::wstring>& symbolExports);
	void SetMaxPayloadSize(const UINT maxPayloadSize) { m_maxPayloadSize = maxPayloadSize; }
	void SetMaxAttributeSize(const UINT maxAttributeSize) { m_maxAttributeSize = maxAttributeSize; }
	ID3D12StateObject* Generate();
private:
	ID3D12Device5* m_device;
	std::vector<LibraryDescription> m_libDesc;
	std::vector<HitGroupDescription> m_hitGroupDesc;
	std::vector<RootSignatureAssociation> m_associations;

	ID3D12RootSignature* m_globalRootSignature;
	ComPtr<ID3D12RootSignature> m_localRootSignature;

	UINT m_maxPayloadSize = 0;
	UINT m_maxAttributeSize = 2 * sizeof(float); // Will we only need this since were using the barycentric coordinates
	UINT m_maxRecursion = 2;

	void BuildExportedSymbolsList(std::vector<LPCWSTR>& exportedSymbols);
};

#endif