#pragma hdrstop
#include "./dx12_shader.h"

using namespace DX12Rendering;

struct builtinComputeShaders_t {
	int index;
	const char* name;
	computeShader_t shader;
} computeBuiltins[] = {
	{ static_cast<UINT>(eComputeShaders::COMPUTE_SKINNED_OFFSET), "compute_skinned_offset",  computeShader_t("Compute Skinned Offset") }
};

computeShader_t* DX12Rendering::GetComputeShader(eComputeShaders shader)
{
	if (shader >= eComputeShaders::COMPUTE_COUNT)
	{
		return nullptr;
	}

	builtinComputeShaders_t& shaderObj = computeBuiltins[static_cast<UINT>(shader)];

	if (shaderObj.shader.apiObject == nullptr)
	{
		CompiledShader* shader = (CompiledShader*)malloc(sizeof(CompiledShader));

		DX12Rendering::LoadHLSLShader(shader, shaderObj.name, COMPUTE);

		shaderObj.shader.apiObject = shader;
	}

	return &shaderObj.shader;
}

DX12Rendering::LibraryDescription::LibraryDescription(const std::string libraryName, const std::vector<std::wstring>& symbolExports) :
	m_symbolDesc()
{
	DX12Rendering::LoadHLSLShader(&m_shader, libraryName.c_str(), DXR);

	m_symbolDesc.reserve(symbolExports.size());
	for (auto& symbol : symbolExports) {
		m_symbolDesc.push_back({ symbol.c_str(), nullptr, D3D12_EXPORT_FLAG_NONE });
	}
}

DX12Rendering::LibraryDescription::~LibraryDescription()
{
	UnloadHLSLShader(&m_shader);
}

void DX12Rendering::LoadHLSLShader(CompiledShader* shader, const char* name, eShader shaderType) {
	idStr inFile;
	inFile.Format("renderprogs\\hlsl\\%s", name);
	inFile.StripFileExtension();

	switch (shaderType) {
	case VERTEX:
		inFile += ".vcso";
		break;

	case PIXEL:
		inFile += ".pcso";
		break;

	case DXR:
		inFile += ".rcso";
		break;

	case COMPUTE:
		inFile += ".ccso";
		break;

	default:
		inFile += ".cso";
	}

	void* data = nullptr;
	shader->size = fileSystem->ReadFile(inFile.c_str(), &data);
	shader->data = static_cast<byte*>(data);
}

void DX12Rendering::UnloadHLSLShader(CompiledShader* shader) {
	if (shader->data != nullptr) {
		assert("TODO");
	}
}

void DX12Rendering::LoadComputePipelineState(computeShader_t* computeShader, ComputeRootSignature* rootSignature, ID3D12PipelineState** ppPipelineState)
{
	assert(ppPipelineState != NULL);

	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	DX12Rendering::CompiledShader* shader = static_cast<DX12Rendering::CompiledShader*>(computeShader->apiObject);

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineState = {};
	computePipelineState.CS = { shader->data, shader->size };

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	computePipelineState.pRootSignature = rootSignature->GetRootSignature();

	DX12Rendering::ThrowIfFailed(device->CreateComputePipelineState(&computePipelineState, IID_PPV_ARGS(ppPipelineState)));
}