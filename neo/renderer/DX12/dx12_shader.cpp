#pragma hdrstop
#include "./dx12_shader.h"

void DX12Rendering::LoadHLSLShader(DX12Rendering::DX12CompiledShader* shader, const char* name, eShader shaderType) {
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

	default:
		inFile += ".cso";
	}

	void* data = NULL;
	shader->size = fileSystem->ReadFile(inFile.c_str(), &data);
	shader->data = static_cast<byte*>(data);
}