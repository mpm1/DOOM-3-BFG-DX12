#pragma hdrstop
#include "./dx12_shader.h"

void DX12Rendering::LoadHLSLShader(DX12Rendering::CompiledShader* shader, const char* name, eShader shaderType) {
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