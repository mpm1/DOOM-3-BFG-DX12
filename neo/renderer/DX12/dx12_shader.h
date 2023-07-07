#ifndef __DX12_SHADER_H__
#define __DX12_SHADER_H__

namespace DX12Rendering {
	#include "./dx12_global.h"

	using namespace DirectX;
	using namespace Microsoft::WRL;

	enum eShader {
		VERTEX,
		PIXEL,
		DXR
	};

	struct CompiledShader
	{
		byte* data;
		size_t size;

		CompiledShader() :
			data(nullptr),
			size(0) {}
	};

	void LoadHLSLShader(CompiledShader* shader, const char* name, eShader shaderType);
	void UnloadHLSLShader(CompiledShader* shader);
}

#endif