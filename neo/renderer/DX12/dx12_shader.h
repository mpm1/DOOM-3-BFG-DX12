#ifndef __DX12_SHADER_H__
#define __DX12_SHADER_H__

#include "./dx12_global.h"

namespace DX12Rendering {
	using namespace DirectX;
	using namespace Microsoft::WRL;

	typedef GUID PSOKey;
	struct PSOKeyComparator
	{
		bool operator()(const PSOKey& a, const PSOKey& b) const
		{
			return memcmp(&a, &b, sizeof(PSOKey)) < 0; // This is specifically a less than comparator for the std::map
		}
	};

	struct PipelineStateObjectEntry
	{
		const PSOKey handle;
		bool isValid;
		ComPtr<ID3D12PipelineState> pipelineState;
		bool useDepthBuffer;

		PipelineStateObjectEntry(const PSOKey handle) :
			handle(handle),
			isValid(false),
			pipelineState(nullptr),
			useDepthBuffer(true)
		{

		}

		~PipelineStateObjectEntry()
		{
			if (pipelineState != nullptr)
			{
				pipelineState->Release();
				pipelineState = nullptr;
			}
		}
	};

	enum eShader {
		VERTEX,
		PIXEL,
		DXR,
		COMPUTE
	};

	enum class eComputeShaders : UINT 
	{
		COMPUTE_SKINNED_OFFSET = 0,
		COMPUTE_HIZ_BUFFER,

		COMPUTE_COUNT
	};

	struct CompiledShader
	{
		byte* data;
		size_t size;

		CompiledShader::CompiledShader() :
			data(nullptr),
			size(0) {}
	};

	struct computeShader_t {
		idStr name;
		void* apiObject;

		computeShader_t(idStr name) :
			name(name),
			apiObject(nullptr)
		{

		}
	};

	computeShader_t* GetComputeShader(eComputeShaders shader);

	void LoadHLSLShader(CompiledShader* shader, const char* name, eShader shaderType);
	void UnloadHLSLShader(CompiledShader* shader);

	struct LibraryDescription {
		DX12Rendering::CompiledShader m_shader;
		std::vector<D3D12_EXPORT_DESC> m_symbolDesc;
		D3D12_DXIL_LIBRARY_DESC m_libDesc = {};

		LibraryDescription(const std::string libraryName, const std::vector<std::wstring>& symbolExports);
		~LibraryDescription();
	};

	void LoadComputePipelineState(computeShader_t* computeShader, ComputeRootSignature* rootSignature, ID3D12PipelineState** ppPipelineState);
}

#endif