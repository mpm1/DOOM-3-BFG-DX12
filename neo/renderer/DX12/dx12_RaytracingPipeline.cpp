#pragma hdrstop

#include "./dx12_RaytracingPipeline.h"

namespace DX12Rendering {
	RaytracingPipeline::RaytracingPipeline(ID3D12Device5* device, ID3D12RootSignature* globalRootSignature)
		: m_device(device),
		m_globalRootSignature(globalRootSignature)
	{
		// Build Empty RootSignature
		D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
		rootDesc.NumParameters = 0;
		rootDesc.pParameters = nullptr;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		ID3DBlob* rootSignature;
		ID3DBlob* error;

		HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignature, &error);
		DX12Rendering::ThrowIfFailed(hr);

		hr = m_device->CreateRootSignature(0, rootSignature->GetBufferPointer(), rootSignature->GetBufferSize(), IID_PPV_ARGS(&m_localRootSignature));
		rootSignature->Release();
		DX12Rendering::ThrowIfFailed(hr);
	}

	RaytracingPipeline::~RaytracingPipeline() {
	}

	void RaytracingPipeline::AddHitGroup(const std::wstring& name, const std::wstring& closestHitSymbol) {
		// For now we are only using Closest Hit Shaders
		m_hitGroupDesc.emplace_back(HitGroupDescription(name, closestHitSymbol));
	}

	void RaytracingPipeline::AddAssocation(ID3D12RootSignature* m_rootSignature, const std::vector<std::wstring>& symbols) {
		m_associations.emplace_back(RootSignatureAssociation(m_rootSignature, symbols));
	}

	void RaytracingPipeline::AddLibrary(const std::string libraryName, const std::vector<std::wstring>& symbolExports)
	{
		m_libDesc.push_back(LibraryDescription(libraryName, symbolExports));
	}

	ID3D12StateObject* RaytracingPipeline::Generate() {
		UINT64 subObjectCount =
			m_libDesc.size() +			// DXIL libraries
			m_hitGroupDesc.size() +		// Hit group declarations
			1 +							// Shader configuration
			1 +                         // Shader payload
			2 * m_associations.size() +	// Root signature declaration + association
			2 +							// Empty global and local root signatures
			1;

		std::vector<D3D12_STATE_SUBOBJECT> subObjects(subObjectCount);
		UINT currentIndex = 0;

		// Add the DXIL Library
		for (LibraryDescription& libDesc : m_libDesc)
		{
			libDesc.m_libDesc.DXILLibrary = { libDesc.m_shader.data, libDesc.m_shader.size };
			libDesc.m_libDesc.NumExports = libDesc.m_symbolDesc.size();
			libDesc.m_libDesc.pExports = (D3D12_EXPORT_DESC*)libDesc.m_symbolDesc.data();

			D3D12_STATE_SUBOBJECT libObject = {};
			libObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
			libObject.pDesc = &libDesc.m_libDesc;
			subObjects[currentIndex++] = libObject;
		}

		// Add the hit group declarations
		for (HitGroupDescription& hitGroup : m_hitGroupDesc) {
			hitGroup.m_hitGroupDesc.HitGroupExport = hitGroup.m_name.c_str();
			hitGroup.m_hitGroupDesc.ClosestHitShaderImport = hitGroup.m_closestHitSymbol.c_str();
			hitGroup.m_hitGroupDesc.AnyHitShaderImport = nullptr;
			hitGroup.m_hitGroupDesc.IntersectionShaderImport = nullptr;

			D3D12_STATE_SUBOBJECT subObject = {};
			subObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
			subObject.pDesc = &hitGroup.m_hitGroupDesc;

			subObjects[currentIndex++] = subObject;
		}

		// Shader Payload
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
		shaderConfig.MaxPayloadSizeInBytes = m_maxPayloadSize;
		shaderConfig.MaxAttributeSizeInBytes = m_maxAttributeSize;

		D3D12_STATE_SUBOBJECT shaderSubObject = {};
		shaderSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		shaderSubObject.pDesc = &shaderConfig;
		subObjects[currentIndex++] = shaderSubObject;

		// Add the exported symbols
		std::vector<LPCWSTR> exportedSymbolPointers = {};
		BuildExportedSymbolsList(exportedSymbolPointers);

		const WCHAR** symbolExports = exportedSymbolPointers.data();
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssociation = {};
		payloadAssociation.NumExports = exportedSymbolPointers.size();
		payloadAssociation.pExports = symbolExports;
		payloadAssociation.pSubobjectToAssociate = &subObjects[currentIndex - 1];

		D3D12_STATE_SUBOBJECT associationObject = {};
		associationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		associationObject.pDesc = &payloadAssociation;
		subObjects[currentIndex++] = associationObject;

		// Add RootSignature Associations
		for (RootSignatureAssociation& association : m_associations) {
			D3D12_STATE_SUBOBJECT rootSignatureObj = {};
			rootSignatureObj.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
			rootSignatureObj.pDesc = &association.m_rootSignature;

			subObjects[currentIndex++] = rootSignatureObj;

			// Add the subobject for the association to the exported shader
			association.m_association.NumExports = static_cast<UINT>(association.m_symbols.size());
			association.m_association.pExports = association.m_symbols.data();
			association.m_association.pSubobjectToAssociate = &subObjects[currentIndex - 1];

			D3D12_STATE_SUBOBJECT rootSignatureAssociationObj = {};
			rootSignatureAssociationObj.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			rootSignatureAssociationObj.pDesc = &association.m_association;

			subObjects[currentIndex++] = rootSignatureAssociationObj;
		}

		// Connect a global root signature
		D3D12_STATE_SUBOBJECT globalRootSignature = {};
		globalRootSignature.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		ID3D12RootSignature* pGlobalRoot = m_globalRootSignature;
		globalRootSignature.pDesc = &pGlobalRoot;

		subObjects[currentIndex++] = globalRootSignature;

		// Connect the local root signature
		D3D12_STATE_SUBOBJECT localRootSignature = {};
		localRootSignature.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		ID3D12RootSignature* pLocalRoot = m_localRootSignature.Get();
		localRootSignature.pDesc = &pLocalRoot;

		subObjects[currentIndex++] = localRootSignature;

		// Create the pipeline configuration
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
		pipelineConfig.MaxTraceRecursionDepth = m_maxRecursion;
		{
			D3D12_STATE_SUBOBJECT stateObject = {};
			stateObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
			stateObject.pDesc = &pipelineConfig;

			subObjects[currentIndex++] = stateObject;
		}

		// Create the pipeline state object
		ID3D12StateObject* rayTracingStateObject = nullptr;

		D3D12_STATE_OBJECT_DESC pipelineDesc = {};
		pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		pipelineDesc.NumSubobjects = currentIndex;
		pipelineDesc.pSubobjects = subObjects.data();

		HRESULT hr = m_device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&rayTracingStateObject));
		DX12Rendering::ThrowIfFailed(hr);
		if (FAILED(hr)) 
		{
			DX12Rendering::FailMessage("Could not create raytracing pipeline state object.");
		}

		return rayTracingStateObject;
	}

	void RaytracingPipeline::BuildExportedSymbolsList(std::vector<LPCWSTR>& exportedSymbols) {
		std::unordered_set<LPCWSTR> exports;

		for (const LibraryDescription& libDesc : m_libDesc)
		{
			for (const auto& name : libDesc.m_symbolDesc) {
				exports.insert(name.Name);
			}
		}

		for (const HitGroupDescription& hitDesc : m_hitGroupDesc)
		{
			exports.erase(hitDesc.m_closestHitSymbol.c_str());
			exports.insert(hitDesc.m_name.c_str());
		}

		// Build the final list
		exportedSymbols.reserve(exports.size());
		for (const auto& name : exports) {
			exportedSymbols.push_back(name);
		}
	}

	RootSignatureAssociation::RootSignatureAssociation(ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols) :
		m_rootSignature(rootSignature),
		m_symbols()
	{
		m_symbols.reserve(symbols.size());
		for (auto& symbol : symbols) {
			m_symbols.push_back(symbol.c_str());
		}
	}

	LibraryDescription::LibraryDescription(const std::string libraryName, const std::vector<std::wstring>& symbolExports) :
		m_symbolDesc()
	{
		DX12Rendering::LoadHLSLShader(&m_shader, libraryName.c_str(), DXR);

		m_symbolDesc.reserve(symbolExports.size());
		for (auto& symbol : symbolExports) {
			m_symbolDesc.push_back({ symbol.c_str(), nullptr, D3D12_EXPORT_FLAG_NONE });
		}
	}

	LibraryDescription::~LibraryDescription()
	{
		DX12Rendering::UnloadHLSLShader(&m_shader);
	}

	HitGroupDescription::HitGroupDescription(std::wstring hitGroupName, std::wstring closestHitSymbol) :
		m_name(std::move(hitGroupName)),
		m_closestHitSymbol(std::move(closestHitSymbol))
	{
	}
}