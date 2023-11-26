#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "./dx12_shader.h"
#include "../tr_local.h"

#include <unordered_map>

// Masking constants used to generate the hash code from the GLState object
static constexpr uint64 STATE_TO_HASH_MASK = ~(0ull | GLS_STENCIL_FUNC_REF_BITS);
static constexpr uint64 HASH_FACE_CULL_SHIFT = GLS_STENCIL_FUNC_REF_SHIFT;

static constexpr uint64 HASH_VARIANT_INDEX_SHIFT = 0;
static constexpr uint64 HASH_PARENT_INDEX_SHIFT = HASH_VARIANT_INDEX_SHIFT + 32;

static int activePipelineState = 0;

D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDescriptors[128];

typedef std::unordered_map<int64, ID3D12PipelineState*> PipelineStateMap;
typedef std::unordered_map<int64, PipelineStateMap> ShaderMap;

ShaderMap shaderMap(128);

D3D12_CULL_MODE CalculateCullMode(const int cullType) {
	switch (cullType) {
	case CT_FRONT_SIDED:
		return D3D12_CULL_MODE_FRONT;
	case CT_BACK_SIDED:
		return D3D12_CULL_MODE_BACK;
	}

	return D3D12_CULL_MODE_NONE;
}

D3D12_FILL_MODE CalculateFillMode(const uint64 stateBits) {
	if (stateBits & GLS_POLYMODE_LINE) {
		return D3D12_FILL_MODE_WIREFRAME;
	}

	return D3D12_FILL_MODE_SOLID;
}

D3D12_DEPTH_STENCIL_DESC CalculateDepthStencilMode(const uint64 stateBits) {
	D3D12_DEPTH_STENCIL_DESC dsDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	dsDesc.DepthEnable = true;

	// Check if we should enable the depth mask
	if (stateBits & GLS_DEPTHMASK) {
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	}
	else {
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	}

	switch (stateBits & GLS_DEPTHFUNC_BITS) {
		//TODO: Check if this needs to be reversed.
		case GLS_DEPTHFUNC_EQUAL:	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL; break;
		case GLS_DEPTHFUNC_ALWAYS:	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS; break;
		case GLS_DEPTHFUNC_LESS:	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
		case GLS_DEPTHFUNC_GREATER:	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL; break;
		default:					dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_NEVER;
	}

	// Calculate the stencil
	if (stateBits & (GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS)) {
		dsDesc.StencilEnable = true;
	}
	else {
		dsDesc.StencilEnable = false;
	}

	if (stateBits & (GLS_STENCIL_FUNC_BITS | GLS_STENCIL_FUNC_REF_BITS | GLS_STENCIL_FUNC_MASK_BITS)) {
		const UINT mask = UINT((stateBits & GLS_STENCIL_FUNC_MASK_BITS) >> GLS_STENCIL_FUNC_MASK_SHIFT);
		D3D12_COMPARISON_FUNC func = D3D12_COMPARISON_FUNC_NEVER;
		
		dsDesc.StencilReadMask = mask;
		dsDesc.StencilWriteMask = mask;

		switch (stateBits & GLS_STENCIL_FUNC_BITS) {
			case GLS_STENCIL_FUNC_NEVER:		func = D3D12_COMPARISON_FUNC_NEVER; break;
			case GLS_STENCIL_FUNC_LESS:			func = D3D12_COMPARISON_FUNC_LESS; break;
			case GLS_STENCIL_FUNC_EQUAL:		func = D3D12_COMPARISON_FUNC_EQUAL; break;
			case GLS_STENCIL_FUNC_LEQUAL:		func = D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
			case GLS_STENCIL_FUNC_GREATER:		func = D3D12_COMPARISON_FUNC_GREATER; break;
			case GLS_STENCIL_FUNC_NOTEQUAL:		func = D3D12_COMPARISON_FUNC_NOT_EQUAL; break;
			case GLS_STENCIL_FUNC_GEQUAL:		func = D3D12_COMPARISON_FUNC_GREATER_EQUAL; break;
			case GLS_STENCIL_FUNC_ALWAYS:		func = D3D12_COMPARISON_FUNC_ALWAYS; break;
			default:							func = D3D12_COMPARISON_FUNC_NEVER; break;
		}

		dsDesc.FrontFace.StencilFunc = func;
		dsDesc.BackFace.StencilFunc = func;
	}

	if (stateBits & (GLS_STENCIL_OP_FAIL_BITS | GLS_STENCIL_OP_ZFAIL_BITS | GLS_STENCIL_OP_PASS_BITS)) {
		D3D12_STENCIL_OP sFail = D3D12_STENCIL_OP_KEEP;
		D3D12_STENCIL_OP zFail = D3D12_STENCIL_OP_KEEP;
		D3D12_STENCIL_OP pass = D3D12_STENCIL_OP_KEEP;

		switch (stateBits & GLS_STENCIL_OP_FAIL_BITS) {
			case GLS_STENCIL_OP_FAIL_KEEP:		sFail = D3D12_STENCIL_OP_KEEP; break;
			case GLS_STENCIL_OP_FAIL_ZERO:		sFail = D3D12_STENCIL_OP_ZERO; break;
			case GLS_STENCIL_OP_FAIL_REPLACE:	sFail = D3D12_STENCIL_OP_REPLACE; break;
			case GLS_STENCIL_OP_FAIL_INCR:		sFail = D3D12_STENCIL_OP_INCR_SAT; break;
			case GLS_STENCIL_OP_FAIL_DECR:		sFail = D3D12_STENCIL_OP_DECR_SAT; break;
			case GLS_STENCIL_OP_FAIL_INVERT:	sFail = D3D12_STENCIL_OP_INVERT; break;
			case GLS_STENCIL_OP_FAIL_INCR_WRAP: sFail = D3D12_STENCIL_OP_INCR; break;
			case GLS_STENCIL_OP_FAIL_DECR_WRAP: sFail = D3D12_STENCIL_OP_DECR; break;
		}

		switch (stateBits & GLS_STENCIL_OP_ZFAIL_BITS) {
			case GLS_STENCIL_OP_ZFAIL_KEEP:		zFail = D3D12_STENCIL_OP_KEEP; break;
			case GLS_STENCIL_OP_ZFAIL_ZERO:		zFail = D3D12_STENCIL_OP_ZERO; break;
			case GLS_STENCIL_OP_ZFAIL_REPLACE:	zFail = D3D12_STENCIL_OP_REPLACE; break;
			case GLS_STENCIL_OP_ZFAIL_INCR:		zFail = D3D12_STENCIL_OP_INCR_SAT; break;
			case GLS_STENCIL_OP_ZFAIL_DECR:		zFail = D3D12_STENCIL_OP_DECR_SAT; break;
			case GLS_STENCIL_OP_ZFAIL_INVERT:	zFail = D3D12_STENCIL_OP_INVERT; break;
			case GLS_STENCIL_OP_ZFAIL_INCR_WRAP: zFail = D3D12_STENCIL_OP_INCR; break;
			case GLS_STENCIL_OP_ZFAIL_DECR_WRAP: zFail = D3D12_STENCIL_OP_DECR; break;
		}

		switch (stateBits & GLS_STENCIL_OP_PASS_BITS) {
			case GLS_STENCIL_OP_PASS_KEEP:		pass = D3D12_STENCIL_OP_KEEP; break;
			case GLS_STENCIL_OP_PASS_ZERO:		pass = D3D12_STENCIL_OP_ZERO; break;
			case GLS_STENCIL_OP_PASS_REPLACE:	pass = D3D12_STENCIL_OP_REPLACE; break;
			case GLS_STENCIL_OP_PASS_INCR:		pass = D3D12_STENCIL_OP_INCR_SAT; break;
			case GLS_STENCIL_OP_PASS_DECR:		pass = D3D12_STENCIL_OP_DECR_SAT; break;
			case GLS_STENCIL_OP_PASS_INVERT:	pass = D3D12_STENCIL_OP_INVERT; break;
			case GLS_STENCIL_OP_PASS_INCR_WRAP: pass = D3D12_STENCIL_OP_INCR; break;
			case GLS_STENCIL_OP_PASS_DECR_WRAP: pass = D3D12_STENCIL_OP_DECR; break;
		}

		dsDesc.FrontFace.StencilFailOp = dsDesc.BackFace.StencilFailOp = sFail;
		dsDesc.FrontFace.StencilDepthFailOp = dsDesc.BackFace.StencilDepthFailOp = zFail;
		dsDesc.FrontFace.StencilPassOp = dsDesc.BackFace.StencilPassOp = pass;

		dsDesc.BackFace = dsDesc.FrontFace;
	}

	return dsDesc;
}

D3D12_BLEND_DESC CalculateBlendMode(const uint64 stateBits) {
	D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	D3D12_BLEND srcFactor = D3D12_BLEND_ONE;
	D3D12_BLEND dstFactor = D3D12_BLEND_ZERO;

	// Set the blend mode
	switch (stateBits & GLS_SRCBLEND_BITS) {
		case GLS_SRCBLEND_ZERO:					srcFactor = D3D12_BLEND_ZERO; break;
		case GLS_SRCBLEND_ONE:					srcFactor = D3D12_BLEND_ONE; break;
		case GLS_SRCBLEND_DST_COLOR:			srcFactor = D3D12_BLEND_DEST_COLOR; break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	srcFactor = D3D12_BLEND_INV_DEST_COLOR; break;
		case GLS_SRCBLEND_SRC_ALPHA:			srcFactor = D3D12_BLEND_SRC_ALPHA; break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	srcFactor = D3D12_BLEND_INV_SRC_ALPHA; break;
		case GLS_SRCBLEND_DST_ALPHA:			srcFactor = D3D12_BLEND_DEST_ALPHA; break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	srcFactor = D3D12_BLEND_INV_DEST_ALPHA; break;
		default:
			assert(!"GL_State: invalid src blend state bits\n");
			break;
	}

	switch (stateBits & GLS_DSTBLEND_BITS) {
		case GLS_DSTBLEND_ZERO:					dstFactor = D3D12_BLEND_ZERO; break;
		case GLS_DSTBLEND_ONE:					dstFactor = D3D12_BLEND_ONE; break;
		case GLS_DSTBLEND_SRC_COLOR:			dstFactor = D3D12_BLEND_SRC_COLOR; break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	dstFactor = D3D12_BLEND_INV_SRC_COLOR; break;
		case GLS_DSTBLEND_SRC_ALPHA:			dstFactor = D3D12_BLEND_SRC_ALPHA; break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	dstFactor = D3D12_BLEND_INV_SRC_ALPHA; break;
		case GLS_DSTBLEND_DST_ALPHA:			dstFactor = D3D12_BLEND_DEST_ALPHA; break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:  dstFactor = D3D12_BLEND_INV_DEST_ALPHA; break;
		default:
			assert(!"GL_State: invalid dst blend state bits\n");
			break;
	}

	blendDesc.RenderTarget[0].SrcBlend = srcFactor;
	blendDesc.RenderTarget[0].DestBlend = dstFactor;
	blendDesc.RenderTarget[0].BlendEnable = !(srcFactor == D3D12_BLEND_ONE && dstFactor == D3D12_BLEND_ZERO);

	// Set the colour masking
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		((stateBits & GLS_REDMASK) ? 0 : D3D12_COLOR_WRITE_ENABLE_RED) |
		((stateBits & GLS_GREENMASK) ? 0 : D3D12_COLOR_WRITE_ENABLE_GREEN) |
		((stateBits & GLS_BLUEMASK) ? 0 : D3D12_COLOR_WRITE_ENABLE_BLUE) |
		((stateBits & GLS_ALPHAMASK) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALPHA);

	// TODO: Setup alpha testing

	return blendDesc;
}

void DX12_ApplyPSOVariant(D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const DX12Rendering::eSurfaceVariant variant)
{
	// Modify vertex data
	switch (variant)
	{
	case DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS:
	case DX12Rendering::VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD:
	{
		static const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	}
		break;

	case DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS_SKINNED:
	case DX12Rendering::VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD_SKINNED:
	{
		static const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	}
		break;

	default:
		break;
	}

	// Modify DepthStencil data
	switch (variant) 
	{
	case DX12Rendering::VARIANT_STENCIL_TWOSIDED:
		psoDesc.DepthStencilState.StencilEnable = true;

		psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
		psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_ZERO;

		psoDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_ZERO;
		psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		break;

	case DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS:
	case DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS_SKINNED:
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			 
		psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR_SAT;

		psoDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_DECR_SAT;
		break;

	case DX12Rendering::VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD:
	case DX12Rendering::VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD_SKINNED:
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_DECR_SAT;
		psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_DECR_SAT;

		psoDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		psoDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_INCR_SAT;
		psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR_SAT;
		break;

	default:
		break;
	}
}

void FillPolygonOffset(D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, uint64 stateBits)
{
	if (stateBits & GLS_POLYGON_OFFSET > 0)
	{
		psoDesc.RasterizerState.SlopeScaledDepthBias = backEnd.glState.polyOfsScale;
		psoDesc.RasterizerState.DepthBias = backEnd.glState.polyOfsBias;
	}
}

void LoadStagePipelineState(int parentState, const DX12Rendering::eSurfaceVariant variant, glstate_t state) {
	// Combine the glStateBits with teh faceCulling and parentState index values.
	// We do not need the stecil ref value as this will be set through the command list. This gives us 8 useable bits.
	int64 stateIndex = (state.glStateBits & STATE_TO_HASH_MASK) | (state.faceCulling << HASH_FACE_CULL_SHIFT);
	int64 shaderIndex = (static_cast<uint64>(variant) << HASH_VARIANT_INDEX_SHIFT) | (static_cast<uint64>(parentState) << HASH_PARENT_INDEX_SHIFT);

	const auto pipelineStateMapContainer = shaderMap.find(shaderIndex);
	PipelineStateMap* pipelineStateMap;

	if (pipelineStateMapContainer == shaderMap.end())
	{
		pipelineStateMap = &shaderMap.emplace(shaderIndex, 128).first->second;
	}
	else
	{
		pipelineStateMap = &pipelineStateMapContainer->second;
	}

	const auto result = pipelineStateMap->find(stateIndex);

	if (result == pipelineStateMap->end()) {
		// No PipelineState found. Create a new one.

		// Define the vertex input layout
		static const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT , 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineDescriptors[parentState];
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

		psoDesc.RasterizerState.CullMode = CalculateCullMode(state.faceCulling);
		psoDesc.RasterizerState.FillMode = CalculateFillMode(state.glStateBits);
		psoDesc.RasterizerState.DepthClipEnable = FALSE;

		psoDesc.BlendState = CalculateBlendMode(state.glStateBits);
		psoDesc.DepthStencilState = CalculateDepthStencilMode(state.glStateBits);
		
		FillPolygonOffset(psoDesc, state.glStateBits);

		DX12_ApplyPSOVariant(psoDesc, variant);

		ID3D12PipelineState* renderState;
		dxRenderer.LoadPipelineState(&psoDesc, &renderState);

		wchar_t resourceName[64];
		wsprintfW(resourceName, L"Shader: 0x%x", stateIndex);
		renderState->SetName(resourceName);

		pipelineStateMap->insert({ stateIndex, renderState });

		dxRenderer.SetActivePipelineState(renderState);
	}
	else {
		dxRenderer.SetActivePipelineState(result->second);
	}
}

bool DX12_ActivatePipelineState(const DX12Rendering::eSurfaceVariant variant) {
	if (activePipelineState < 0) {
		return false;
	}

	LoadStagePipelineState(activePipelineState, variant, backEnd.glState);

	return true;
}

void idRenderProgManager::SetUniformValue(const renderParm_t rp, const float* value) {
	dxRenderer.Uniform4f(rp, value);
}

int idRenderProgManager::FindProgram(const char* name, int vIndex, int fIndex) {
	for (int i = 0; i < shaderPrograms.Num(); ++i) {
		if ((shaderPrograms[i].vertexShaderIndex == vIndex) && (shaderPrograms[i].fragmentShaderIndex == fIndex)) {
			LoadProgram(i, vIndex, fIndex); //TODO: We need a better way to pass the render target count.
			return i;
		}
	}

	hlslProgram_t program;
	program.name = name;
	int index = shaderPrograms.Append(program);
	LoadProgram(index, vIndex, fIndex);
	return index;
}

/// <summary>
/// Creates a base pipeline state descriptor. This will be used later to generate the material pipeline states.
/// </summary>
/// <param name="programIndex"></param>
/// <param name="vertexShaderIndex"></param>
/// <param name="fragmentShaderIndex"></param>
void idRenderProgManager::LoadProgram(const int programIndex, const int vertexShaderIndex, const int fragmentShaderIndex) {
	// TODO: Implment
	hlslProgram_t & prog = shaderPrograms[programIndex];

	if (prog.shaderObject != NULL) {
		return; // Already loaded.
	}

	DX12Rendering::CompiledShader* vertexShader = (vertexShaderIndex != -1) ? static_cast<DX12Rendering::CompiledShader*>(vertexShaders[vertexShaderIndex].apiObject) : NULL;
	DX12Rendering::CompiledShader* fragmentShader = (fragmentShaderIndex != -1) ? static_cast<DX12Rendering::CompiledShader*>(fragmentShaders[fragmentShaderIndex].apiObject) : NULL;

	if (vertexShader == NULL || fragmentShader == NULL || vertexShader->data == NULL || fragmentShader->data == NULL) {
		common->Warning("Could not build shader %s.", vertexShaders[vertexShaderIndex].name.c_str());
		return;
	}

	CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);
	rasterizerState.FrontCounterClockwise = TRUE; // This is done to match the opengl direction.
	rasterizerState.CullMode = D3D12_CULL_MODE_NONE; // Default front and back.

	CD3DX12_BLEND_DESC blendState(D3D12_DEFAULT); // TODO: set the blend state. We need to load these pipeline states for entire materials and not just programs.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC* psoDesc = &pipelineDescriptors[programIndex];
	psoDesc->VS = { vertexShader->data, vertexShader->size };
	psoDesc->PS = { fragmentShader->data, fragmentShader->size };
	psoDesc->RasterizerState = rasterizerState;
	psoDesc->BlendState = blendState;
	psoDesc->SampleMask = UINT_MAX;
	psoDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc->NumRenderTargets = MAX_RENDER_TARGETS;
	psoDesc->RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc->DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc->SampleDesc.Count = 1;

	prog.shaderObject = psoDesc;
}

/*
================================================================================================
idRenderProgManager::LoadVertexShader
================================================================================================
*/
void idRenderProgManager::LoadVertexShader(int index) {
	if ( vertexShaders[index].apiObject != NULL ) {
		return; // Already loaded
	}

	DX12Rendering::CompiledShader* shader = (DX12Rendering::CompiledShader*)malloc(sizeof(DX12Rendering::CompiledShader));

	DX12Rendering::LoadHLSLShader(shader, vertexShaders[index].name, DX12Rendering::VERTEX);

	vertexShaders[index].apiObject = shader;
	//vertexShaders[index].progId = ( GLuint ) LoadGLSLShader( GL_VERTEX_SHADER, vertexShaders[index].name, vertexShaders[index].uniforms );*/
}

/*
================================================================================================
idRenderProgManager::LoadFragmentShader
================================================================================================
*/
void idRenderProgManager::LoadFragmentShader(int index) {
	if (fragmentShaders[index].apiObject != NULL) {
		return; // Already loaded
	}

	DX12Rendering::CompiledShader* shader = (DX12Rendering::CompiledShader*)malloc(sizeof(DX12Rendering::CompiledShader));

	DX12Rendering::LoadHLSLShader(shader, fragmentShaders[index].name, DX12Rendering::PIXEL);

	fragmentShaders[index].apiObject = shader;

	//fragmentShaders[index].progId = ( GLuint ) LoadGLSLShader( GL_FRAGMENT_SHADER, fragmentShaders[index].name, fragmentShaders[index].uniforms );
}

/*
================================================================================================
idRenderProgManager::BindShader
================================================================================================
*/
void idRenderProgManager::BindShader(int vIndex, int fIndex) {
	if (currentVertexShader == vIndex && currentFragmentShader == fIndex) {
		return;
	}

	currentVertexShader = vIndex;
	currentFragmentShader = fIndex;

	// vIndex denotes the GLSL program
	if (vIndex >= 0 && vIndex < shaderPrograms.Num()) {
		if (shaderPrograms[vIndex].shaderObject == NULL) {
			common->Warning("RenderState %s has not been loaded.", vertexShaders[vIndex].name.c_str());
			currentRenderProgram = -1;
			activePipelineState = -1;
			return;
		}

		currentRenderProgram = vIndex;
		RENDERLOG_PRINTF("Binding RenderState %s\n", shaderPrograms[vIndex].name.c_str());

		activePipelineState = vIndex;
	}
	else {
		common->Warning("Shader index is out of range.");
	}
}

/*
================================================================================================
idRenderProgManager::SetRenderParm
================================================================================================
*/
void idRenderProgManager::SetRenderParm(renderParm_t rp, const float* value) {
	// TODO: Set the property.
	dxRenderer.Uniform4f(rp, value);
}