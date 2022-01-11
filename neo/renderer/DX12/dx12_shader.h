#ifndef __DX12_SHADER_H__
#define __DX12_SHADER_H__

namespace DX12Rendering {
	#include "./dx12_global.h"

	using namespace DirectX;
	using namespace Microsoft::WRL;

	enum eShader {
		VERTEX,
		PIXEL
	};

	struct DX12CompiledShader
	{
		byte* data;
		size_t size;
	};

	void LoadHLSLShader(DX12CompiledShader* shader, const char* name, eShader shaderType);
}

#endif