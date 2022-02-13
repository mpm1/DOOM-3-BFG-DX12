#pragma hdrstop

#include "./dx12_RaytracingPipeline.h"

namespace DX12Rendering {
	RaytracingPipeline::RaytracingPipeline(ID3D12Device5* device, const char* libraryName, ID3D12RootSignature* globalRootSignature, const std::vector<LPCWSTR>& symbolExports)
		: m_device(device),
		m_globalRootSignature(globalRootSignature)
	{
		for (const LPCWSTR& symbol : symbolExports) {
			m_symbolDesc.push_back({ symbol, nullptr, D3D12_EXPORT_FLAG_NONE });
		}

		DX12Rendering::LoadHLSLShader(&m_shader, libraryName, DXR);

		m_libDesc = {};
		m_libDesc.DXILLibrary = { m_shader.data, m_shader.size };
		m_libDesc.NumExports = m_symbolDesc.size();
		m_libDesc.pExports = (D3D12_EXPORT_DESC*)m_symbolDesc.data();

		// Build Empty RootSignature
		D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
		rootDesc.NumParameters = 0;
		rootDesc.pParameters = nullptr;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		ID3DBlob* rootSignature;
		ID3DBlob* error;

		HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSignature, &error);
		DX12ThrowIfFailed(hr);

		hr = m_device->CreateRootSignature(0, rootSignature->GetBufferPointer(), rootSignature->GetBufferSize(), IID_PPV_ARGS(&m_localRootSignature));
		rootSignature->Release();
		DX12ThrowIfFailed(hr);
	}

	RaytracingPipeline::~RaytracingPipeline() {
		DX12Rendering::UnloadHLSLShader(&m_shader);
	}

	void RaytracingPipeline::AddHitGroup(const std::wstring& name, const std::wstring& closestHitSymbol) {
		// For now we are only using Closest Hit Shaders
		D3D12_HIT_GROUP_DESC hitGroupDesc = {};
		hitGroupDesc.HitGroupExport = name.c_str();
		hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
		hitGroupDesc.ClosestHitShaderImport = closestHitSymbol.c_str();

		m_hitGroupDesc.emplace_back(hitGroupDesc);
	}

	void RaytracingPipeline::AddAssocation(ID3D12RootSignature* m_rootSignature, const std::vector<std::wstring>& symbols) {
		m_associations.emplace_back(RootSignatureAssociation(m_rootSignature, symbols));
	}

	ID3D12StateObject* RaytracingPipeline::Generate() {
		UINT64 subObjectCount =
			1 +							// DXIL libraries
			m_hitGroupDesc.size() +		// Hit group declarations
			1 +							// Shader configuration
			1 +                         // Shader payload
			2 * m_associations.size() +	// Root signature declaration + association
			2 +							// Empty global and local root signatures
			1;

		std::vector<D3D12_STATE_SUBOBJECT> subObjects(subObjectCount);
		UINT currentIndex = 0;

		// Add the DXIL Library
		D3D12_STATE_SUBOBJECT libObject = {};
		libObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		libObject.pDesc = &m_libDesc;
		subObjects[currentIndex++] = libObject;

		// Add the hit group declarations
		for (const D3D12_HIT_GROUP_DESC& hitGroup : m_hitGroupDesc) {
			D3D12_STATE_SUBOBJECT subObject = {};
			subObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
			subObject.pDesc = &hitGroup;

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
		exportedSymbolPointers.reserve(m_symbolDesc.size());
		for (const auto& name : m_symbolDesc)
		{
			exportedSymbolPointers.emplace_back(name.Name);
		}

		const WCHAR** symbolExports = exportedSymbolPointers.data();
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssociation = {};
		payloadAssociation.NumExports = m_symbolDesc.size();
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
		DX12ThrowIfFailed(hr);
		/*if (FAILED(hr)) 
		{
			DX12FailMessage("Could not create raytracing pipeline state object.");
		}*/

		return rayTracingStateObject;
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
}