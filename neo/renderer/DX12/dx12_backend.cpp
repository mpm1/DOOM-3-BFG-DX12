#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "../tr_local.h"
#include "../../framework/Common_local.h"

#include <memory>

idCVar r_drawEyeColor("r_drawEyeColor", "0", CVAR_RENDERER | CVAR_BOOL, "Draw a colored box, red = left eye, blue = right eye, grey = non-stereo");
idCVar r_motionBlur("r_motionBlur", "0", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "1 - 5, log2 of the number of motion blur samples");
idCVar r_forceZPassStencilShadows("r_forceZPassStencilShadows", "0", CVAR_RENDERER | CVAR_BOOL, "force Z-pass rendering for performance testing");
idCVar r_useStencilShadowPreload("r_useStencilShadowPreload", "1", CVAR_RENDERER | CVAR_BOOL, "use stencil shadow preload algorithm instead of Z-fail");
idCVar r_skipShaderPasses("r_skipShaderPasses", "0", CVAR_RENDERER | CVAR_BOOL, "");
idCVar r_skipInteractionFastPath("r_skipInteractionFastPath", "1", CVAR_RENDERER | CVAR_BOOL, "");
idCVar r_useLightStencilSelect("r_useLightStencilSelect", "0", CVAR_RENDERER | CVAR_BOOL, "use stencil select pass");

extern idCVar stereoRender_swapEyes;

backEndState_t	backEnd;

struct GBufferSurfaceConstants
{
	UINT bumpMapIndex;
	UINT albedoIndex;
	UINT specularIndex;
	UINT pad;
};

// Loads a texture into memory and provides the bindless index to that texture
UINT LoadBindlessTexture(idImage* image)
{
	auto buffer = static_cast<const DX12Rendering::TextureBuffer*>(image->Bindless());

	if (buffer == nullptr)
	{
		return 0;
	}

	return buffer->GetTextureIndex();
}


int GetVertexBufferOffset(const vertCacheHandle_t vbHandle)
{
	// TODO: Add check here to grab the current frames joint model offset if it exists.
	return (int)(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
}

UINT GetVertexBufferSize(const vertCacheHandle_t vbHandle)
{
	return (vbHandle >> VERTCACHE_SIZE_SHIFT) & VERTCACHE_SIZE_MASK;
}

idVertexBuffer* GetVertexBuffer(const vertCacheHandle_t vbHandle)
{
	// TODO: Add check here to grab the current frames joint model if it exists.
	// get vertex buffer
	idVertexBuffer* vertexBuffer;
	if (vertexCache.CacheIsStatic(vbHandle)) {
		vertexBuffer = &vertexCache.staticData.vertexBuffer;
	}
	else {
		const uint64 frameNum = (int)(vbHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
		if (frameNum != ((vertexCache.currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
			idLib::Warning("RB_DrawElementsWithCounters, vertexBuffer == NULL");
			return nullptr;
		}
		vertexBuffer = &vertexCache.frameData[vertexCache.drawListNum].vertexBuffer;
	}

	return vertexBuffer;
}
	

bool R_GetModeListForDisplay(const int displayNum, idList<vidMode_t>& modeList) 
{
	bool displayFound = DX12Rendering::Device::GetAllSupportedResolutions(displayNum, [&modeList](UINT monitor, UINT subIndex, UINT width, UINT height, UINT refreshNumerator, UINT refreshDenominator) -> void
	{
		vidMode_t mode;

		assert(refreshDenominator != 0);

		mode.displayHz = refreshNumerator / refreshDenominator;
		mode.height = height;
		mode.width = width;

		modeList.Append(mode);
	});

	return displayFound;
}

/*
================
SetVertexParm
================
*/
static ID_INLINE void SetVertexParm(renderParm_t rp, const float* value) 
{
	renderProgManager.SetUniformValue(rp, value);
}

/*
================
SetVertexParms
================
*/
static ID_INLINE void SetVertexParms(renderParm_t rp, const float* value, int num) 
{
	for (int i = 0; i < num; i++) {
		renderProgManager.SetUniformValue((renderParm_t)(rp + i), value + (i * 4));
	}
}

/*
================
SetFragmentParm
================
*/
static ID_INLINE void SetFragmentParm(renderParm_t rp, const float* value) 
{
	renderProgManager.SetUniformValue(rp, value);
}

/*
================
RB_SetMVP
================
*/
void RB_SetMVP(const idRenderMatrix& mvp) {
	SetVertexParms(RENDERPARM_MVPMATRIX_X, mvp[0], 4);
}

static const float zero[4] = { 0, 0, 0, 0 };
static const float one[4] = { 1, 1, 1, 1 };
static const float negOne[4] = { -1, -1, -1, -1 };

void RB_SetBuffer() {
	// TODO: Implement
	//GL_Clear(true, false, false, STENCIL_SHADOW_MASK_VALUE, 0, 0, 0, 1.0f);
}

/*
================
RB_SetVertexColorParms
================
*/
static void RB_SetVertexColorParms(stageVertexColor_t svc) {
	switch (svc) {
	case SVC_IGNORE:
		SetVertexParm(RENDERPARM_VERTEXCOLOR_MODULATE, zero);
		SetVertexParm(RENDERPARM_VERTEXCOLOR_ADD, one);
		break;
	case SVC_MODULATE:
		SetVertexParm(RENDERPARM_VERTEXCOLOR_MODULATE, one);
		SetVertexParm(RENDERPARM_VERTEXCOLOR_ADD, zero);
		break;
	case SVC_INVERSE_MODULATE:
		SetVertexParm(RENDERPARM_VERTEXCOLOR_MODULATE, negOne);
		SetVertexParm(RENDERPARM_VERTEXCOLOR_ADD, one);
		break;
	}
}

/*
======================
RB_GetShaderTextureMatrix
======================
*/
static void RB_GetShaderTextureMatrix(const float* shaderRegisters, const textureStage_t* texture, float matrix[16]) {
	matrix[0 * 4 + 0] = shaderRegisters[texture->matrix[0][0]];
	matrix[1 * 4 + 0] = shaderRegisters[texture->matrix[0][1]];
	matrix[2 * 4 + 0] = 0.0f;
	matrix[3 * 4 + 0] = shaderRegisters[texture->matrix[0][2]];

	matrix[0 * 4 + 1] = shaderRegisters[texture->matrix[1][0]];
	matrix[1 * 4 + 1] = shaderRegisters[texture->matrix[1][1]];
	matrix[2 * 4 + 1] = 0.0f;
	matrix[3 * 4 + 1] = shaderRegisters[texture->matrix[1][2]];

	// we attempt to keep scrolls from generating incredibly large texture values, but
	// center rotations and center scales can still generate offsets that need to be > 1
	if (matrix[3 * 4 + 0] < -40.0f || matrix[12] > 40.0f) {
		matrix[3 * 4 + 0] -= (int)matrix[3 * 4 + 0];
	}
	if (matrix[13] < -40.0f || matrix[13] > 40.0f) {
		matrix[13] -= (int)matrix[13];
	}

	matrix[0 * 4 + 2] = 0.0f;
	matrix[1 * 4 + 2] = 0.0f;
	matrix[2 * 4 + 2] = 1.0f;
	matrix[3 * 4 + 2] = 0.0f;

	matrix[0 * 4 + 3] = 0.0f;
	matrix[1 * 4 + 3] = 0.0f;
	matrix[2 * 4 + 3] = 0.0f;
	matrix[3 * 4 + 3] = 1.0f;
}

/*
======================
RB_LoadShaderTextureMatrix
======================
*/
static void RB_LoadShaderTextureMatrix(const float* shaderRegisters, const textureStage_t* texture) {
	float texS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float texT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	if (texture->hasMatrix) {
		float matrix[16];
		RB_GetShaderTextureMatrix(shaderRegisters, texture, matrix);
		texS[0] = matrix[0 * 4 + 0];
		texS[1] = matrix[1 * 4 + 0];
		texS[2] = matrix[2 * 4 + 0];
		texS[3] = matrix[3 * 4 + 0];

		texT[0] = matrix[0 * 4 + 1];
		texT[1] = matrix[1 * 4 + 1];
		texT[2] = matrix[2 * 4 + 1];
		texT[3] = matrix[3 * 4 + 1];

		RENDERLOG_PRINTF("Setting Texture Matrix\n");
		renderLog.Indent();
		RENDERLOG_PRINTF("Texture Matrix S : %4.3f, %4.3f, %4.3f, %4.3f\n", texS[0], texS[1], texS[2], texS[3]);
		RENDERLOG_PRINTF("Texture Matrix T : %4.3f, %4.3f, %4.3f, %4.3f\n", texT[0], texT[1], texT[2], texT[3]);
		renderLog.Outdent();
	}

	SetVertexParm(RENDERPARM_TEXTUREMATRIX_S, texS);
	SetVertexParm(RENDERPARM_TEXTUREMATRIX_T, texT);
}

/*
=====================
RB_BakeTextureMatrixIntoTexgen
=====================
*/
static void RB_BakeTextureMatrixIntoTexgen(idPlane lightProject[3], const float* textureMatrix) {
	float genMatrix[16];
	float final[16];

	genMatrix[0 * 4 + 0] = lightProject[0][0];
	genMatrix[1 * 4 + 0] = lightProject[0][1];
	genMatrix[2 * 4 + 0] = lightProject[0][2];
	genMatrix[3 * 4 + 0] = lightProject[0][3];

	genMatrix[0 * 4 + 1] = lightProject[1][0];
	genMatrix[1 * 4 + 1] = lightProject[1][1];
	genMatrix[2 * 4 + 1] = lightProject[1][2];
	genMatrix[3 * 4 + 1] = lightProject[1][3];

	genMatrix[0 * 4 + 2] = 0.0f;
	genMatrix[1 * 4 + 2] = 0.0f;
	genMatrix[2 * 4 + 2] = 0.0f;
	genMatrix[3 * 4 + 2] = 0.0f;

	genMatrix[0 * 4 + 3] = lightProject[2][0];
	genMatrix[1 * 4 + 3] = lightProject[2][1];
	genMatrix[2 * 4 + 3] = lightProject[2][2];
	genMatrix[3 * 4 + 3] = lightProject[2][3];

	R_MatrixMultiply(genMatrix, textureMatrix, final);

	lightProject[0][0] = final[0 * 4 + 0];
	lightProject[0][1] = final[1 * 4 + 0];
	lightProject[0][2] = final[2 * 4 + 0];
	lightProject[0][3] = final[3 * 4 + 0];

	lightProject[1][0] = final[0 * 4 + 1];
	lightProject[1][1] = final[1 * 4 + 1];
	lightProject[1][2] = final[2 * 4 + 1];
	lightProject[1][3] = final[3 * 4 + 1];
}

/*
================
RB_FinishStageTexturing
================
*/
static void RB_FinishStageTexturing(const shaderStage_t* pStage, const drawSurf_t* surf) {

	if (pStage->texture.cinematic) {
		// unbind the extra bink textures
		GL_SelectTexture(1);
		globalImages->BindNull();
		GL_SelectTexture(2);
		globalImages->BindNull();
		GL_SelectTexture(0);
	}

	if (pStage->texture.texgen == TG_REFLECT_CUBE) {
		// see if there is also a bump map specified
		const shaderStage_t* bumpStage = surf->material->GetBumpStage();
		if (bumpStage != NULL) {
			// per-pixel reflection mapping with bump mapping
			GL_SelectTexture(1);
			globalImages->BindNull();
			GL_SelectTexture(0);
		}
		else {
			// per-pixel reflection mapping without bump mapping
		}
		renderProgManager.Unbind();
	}
}

/*
================
RB_PrepareStageTexturing
================
*/
static void RB_PrepareStageTexturing(const shaderStage_t* pStage, const drawSurf_t* surf) {
	float useTexGenParm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// set the texture matrix if needed
	RB_LoadShaderTextureMatrix(surf->shaderRegisters, &pStage->texture);

	// texgens
	if (pStage->texture.texgen == TG_REFLECT_CUBE) {

		// see if there is also a bump map specified
		const shaderStage_t* bumpStage = surf->material->GetBumpStage();
		if (bumpStage != NULL) {
			// per-pixel reflection mapping with bump mapping
			// TODO: Note this for futer RTX reflections
			GL_SelectTexture(1);
			bumpStage->texture.image->Bind();
			GL_SelectTexture(0);

			RENDERLOG_PRINTF("TexGen: TG_REFLECT_CUBE: Bumpy Environment\n");
			if (surf->jointCache) {
				renderProgManager.BindShader_BumpyEnvironmentSkinned();
			}
			else {
				renderProgManager.BindShader_BumpyEnvironment();
			}
		}
		else {
			RENDERLOG_PRINTF("TexGen: TG_REFLECT_CUBE: Environment\n");
			if (surf->jointCache) {
				renderProgManager.BindShader_EnvironmentSkinned();
			}
			else {
				renderProgManager.BindShader_Environment();
			}
		}

	}
	else if (pStage->texture.texgen == TG_SKYBOX_CUBE) {

		renderProgManager.BindShader_SkyBox();

	}
	else if (pStage->texture.texgen == TG_WOBBLESKY_CUBE) {

		const int* parms = surf->material->GetTexGenRegisters();

		float wobbleDegrees = surf->shaderRegisters[parms[0]] * (idMath::PI / 180.0f);
		float wobbleSpeed = surf->shaderRegisters[parms[1]] * (2.0f * idMath::PI / 60.0f);
		float rotateSpeed = surf->shaderRegisters[parms[2]] * (2.0f * idMath::PI / 60.0f);

		idVec3 axis[3];
		{
			// very ad-hoc "wobble" transform
			float s, c;
			idMath::SinCos(wobbleSpeed * backEnd.viewDef->renderView.time[0] * 0.001f, s, c);

			float ws, wc;
			idMath::SinCos(wobbleDegrees, ws, wc);

			axis[2][0] = ws * c;
			axis[2][1] = ws * s;
			axis[2][2] = wc;

			axis[1][0] = -s * s * ws;
			axis[1][2] = -s * ws * ws;
			axis[1][1] = idMath::Sqrt(idMath::Fabs(1.0f - (axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2])));

			// make the second vector exactly perpendicular to the first
			axis[1] -= (axis[2] * axis[1]) * axis[2];
			axis[1].Normalize();

			// construct the third with a cross
			axis[0].Cross(axis[1], axis[2]);
		}

		// add the rotate
		float rs, rc;
		idMath::SinCos(rotateSpeed * backEnd.viewDef->renderView.time[0] * 0.001f, rs, rc);

		float transform[12];
		transform[0 * 4 + 0] = axis[0][0] * rc + axis[1][0] * rs;
		transform[0 * 4 + 1] = axis[0][1] * rc + axis[1][1] * rs;
		transform[0 * 4 + 2] = axis[0][2] * rc + axis[1][2] * rs;
		transform[0 * 4 + 3] = 0.0f;

		transform[1 * 4 + 0] = axis[1][0] * rc - axis[0][0] * rs;
		transform[1 * 4 + 1] = axis[1][1] * rc - axis[0][1] * rs;
		transform[1 * 4 + 2] = axis[1][2] * rc - axis[0][2] * rs;
		transform[1 * 4 + 3] = 0.0f;

		transform[2 * 4 + 0] = axis[2][0];
		transform[2 * 4 + 1] = axis[2][1];
		transform[2 * 4 + 2] = axis[2][2];
		transform[2 * 4 + 3] = 0.0f;

		SetVertexParms(RENDERPARM_WOBBLESKY_X, transform, 3);
		renderProgManager.BindShader_WobbleSky();

	}
	else if ((pStage->texture.texgen == TG_SCREEN) || (pStage->texture.texgen == TG_SCREEN2)) {

		useTexGenParm[0] = 1.0f;
		useTexGenParm[1] = 1.0f;
		useTexGenParm[2] = 1.0f;
		useTexGenParm[3] = 1.0f;

		float mat[16];
		R_MatrixMultiply(surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat);

		RENDERLOG_PRINTF("TexGen : %s\n", (pStage->texture.texgen == TG_SCREEN) ? "TG_SCREEN" : "TG_SCREEN2");
		renderLog.Indent();

		float plane[4];
		plane[0] = mat[0 * 4 + 0];
		plane[1] = mat[1 * 4 + 0];
		plane[2] = mat[2 * 4 + 0];
		plane[3] = mat[3 * 4 + 0];
		SetVertexParm(RENDERPARM_TEXGEN_0_S, plane);
		RENDERLOG_PRINTF("TEXGEN_S = %4.3f, %4.3f, %4.3f, %4.3f\n", plane[0], plane[1], plane[2], plane[3]);

		plane[0] = mat[0 * 4 + 1];
		plane[1] = mat[1 * 4 + 1];
		plane[2] = mat[2 * 4 + 1];
		plane[3] = mat[3 * 4 + 1];
		SetVertexParm(RENDERPARM_TEXGEN_0_T, plane);
		RENDERLOG_PRINTF("TEXGEN_T = %4.3f, %4.3f, %4.3f, %4.3f\n", plane[0], plane[1], plane[2], plane[3]);

		plane[0] = mat[0 * 4 + 3];
		plane[1] = mat[1 * 4 + 3];
		plane[2] = mat[2 * 4 + 3];
		plane[3] = mat[3 * 4 + 3];
		SetVertexParm(RENDERPARM_TEXGEN_0_Q, plane);
		RENDERLOG_PRINTF("TEXGEN_Q = %4.3f, %4.3f, %4.3f, %4.3f\n", plane[0], plane[1], plane[2], plane[3]);

		renderLog.Outdent();

	}
	else if (pStage->texture.texgen == TG_DIFFUSE_CUBE) {

		// As far as I can tell, this is never used
		idLib::Warning("Using Diffuse Cube! Please contact Brian!");

	}
	else if (pStage->texture.texgen == TG_GLASSWARP) {

		// As far as I can tell, this is never used
		idLib::Warning("Using GlassWarp! Please contact Brian!");
	}

	SetVertexParm(RENDERPARM_TEXGEN_0_ENABLED, useTexGenParm);
}

void RB_DrawElementsWithCounters(const drawSurf_t* surf, const vertCacheHandle_t vbHandle, const size_t vertexStride, const DX12Rendering::eSurfaceVariant variant, void* surfaceConstants, size_t surfaceConstantsSize) {
	DX12Rendering::Commands::CommandList* commandList = DX12Rendering::RenderPassBlock::GetCurrentRenderPass()->GetCommandManager()->RequestNewCommandList();
	DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "RB_DrawElementsWithCounters");

	dxRenderer.SetCommandListDefaults(commandList, false);

	// Connect to a new surfae renderer
	const UINT gpuIndex = dxRenderer.StartSurfaceSettings();

	// get vertex buffer
	idVertexBuffer* vertexBuffer = GetVertexBuffer(vbHandle);
	const int vertOffset = GetVertexBufferOffset(vbHandle);

	if (vertexBuffer == nullptr)
	{
		// An error occured when grabbing the vertex buffer data.
		return;
	}

	auto apiVertexBuffer = reinterpret_cast<DX12Rendering::Geometry::VertexBuffer*>(vertexBuffer->GetAPIObject());
	
	// get index buffer
	const vertCacheHandle_t ibHandle = surf->indexCache;
	idIndexBuffer* indexBuffer;
	if (vertexCache.CacheIsStatic(ibHandle)) {
		indexBuffer = &vertexCache.staticData.indexBuffer;
	}
	else {
		const uint64 frameNum = (int)(ibHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
		if (frameNum != ((vertexCache.currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
			idLib::Warning("RB_DrawElementsWithCounters, indexBuffer == NULL");
			return;
		}
		indexBuffer = &vertexCache.frameData[vertexCache.drawListNum].indexBuffer;
	}
	const UINT indexOffset = static_cast<UINT>(ibHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

	RENDERLOG_PRINTF("Binding Buffers: %p:%i %p:%i\n", vertexBuffer, vertOffset, indexBuffer, indexOffset);

	if (surf->jointCache) {
		if (!verify(renderProgManager.ShaderUsesJoints())) {
			return;
		}
	}
	else {
		if (!verify(!renderProgManager.ShaderUsesJoints() || renderProgManager.ShaderHasOptionalSkinning())) {
			return;
		}
	}

	if (surf->jointCache) {
		idJointBuffer jointBuffer;
		if (!vertexCache.GetJointBuffer(surf->jointCache, &jointBuffer)) {
			idLib::Warning("RB_DrawElementsWithCounters, jointBuffer == NULL");
			return;
		}
		assert((jointBuffer.GetOffset() & (glConfig.uniformBufferOffsetAlignment - 1)) == 0);

		dxRenderer.SetJointBuffer(reinterpret_cast<DX12Rendering::Geometry::JointBuffer*>(jointBuffer.GetAPIObject()), jointBuffer.GetOffset(), commandList);
	}

	if (dxRenderer.EndSurfaceSettings(variant, surfaceConstants, surfaceConstantsSize, *commandList)) {
		dxRenderer.DrawModel(
			*commandList,
			apiVertexBuffer,
			vertOffset / static_cast<int>(((vertexStride > 0) ? vertexStride : sizeof(idDrawVert))),
			reinterpret_cast<DX12Rendering::Geometry::IndexBuffer*>(indexBuffer->GetAPIObject()),
			indexOffset >> 1, // TODO: Figure out why we need to divide by 2. Is it because we are going from an int to a short?
			r_singleTriangle.GetBool() ? 3 : surf->numIndexes,
			static_cast<UINT>(vertexStride));

		//TODO: Eventually do the creation of the acceleration structure outside of these commands.
	}

	/*if (backEnd.glState.currentIndexBuffer != (GLuint)indexBuffer->GetAPIObject() || !r_useStateCaching.GetBool()) {
		qglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, (GLuint)indexBuffer->GetAPIObject());
		backEnd.glState.currentIndexBuffer = (GLuint)indexBuffer->GetAPIObject();
	}

	if ((backEnd.glState.vertexLayout != LAYOUT_DRAW_VERT) || (backEnd.glState.currentVertexBuffer != (GLuint)vertexBuffer->GetAPIObject()) || !r_useStateCaching.GetBool()) {
		qglBindBufferARB(GL_ARRAY_BUFFER_ARB, (GLuint)vertexBuffer->GetAPIObject());
		backEnd.glState.currentVertexBuffer = (GLuint)vertexBuffer->GetAPIObject();

		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_VERTEX);
		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_NORMAL);
		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_COLOR);
		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_COLOR2);
		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_ST);
		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_TANGENT);

		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(idDrawVert), (void*)(DRAWVERT_XYZ_OFFSET));
		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_NORMAL, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(idDrawVert), (void*)(DRAWVERT_NORMAL_OFFSET));
		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(idDrawVert), (void*)(DRAWVERT_COLOR_OFFSET));
		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_COLOR2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(idDrawVert), (void*)(DRAWVERT_COLOR2_OFFSET));
		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_ST, 2, GL_HALF_FLOAT, GL_TRUE, sizeof(idDrawVert), (void*)(DRAWVERT_ST_OFFSET));
		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_TANGENT, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(idDrawVert), (void*)(DRAWVERT_TANGENT_OFFSET));

		backEnd.glState.vertexLayout = LAYOUT_DRAW_VERT;
	}

	qglDrawElementsBaseVertex(GL_TRIANGLES,
		r_singleTriangle.GetBool() ? 3 : surf->numIndexes,
		GL_INDEX_TYPE,
		(triIndex_t*)indexOffset,
		vertOffset / sizeof(idDrawVert));*/
}

void RB_DrawElementsWithCounters(const drawSurf_t* surf, void* surfaceConstants, size_t bufferSize) {
	RB_DrawElementsWithCounters(surf, surf->ambientCache, 0 /* Keep the value in the view */, DX12Rendering::VARIANT_DEFAULT, surfaceConstants, bufferSize);
}

void RB_DrawElementsWithCounters(const drawSurf_t* surf) {
	RB_DrawElementsWithCounters(surf, nullptr, 0);
}

/*
==================
RB_FillDepthBufferGeneric
==================
*/
static void RB_FillDepthBufferGeneric(const drawSurf_t* const* drawSurfs, int numDrawSurfs) {
	for (int i = 0; i < numDrawSurfs; i++) {
		const drawSurf_t* drawSurf = drawSurfs[i];
		const idMaterial* shader = drawSurf->material;

		// translucent surfaces don't put anything in the depth buffer and don't
		// test against it, which makes them fail the mirror clip plane operation
		if (shader->Coverage() == MC_TRANSLUCENT) {
			continue;
		}

		{
			// Removed block as it slowed down rendering.
			//auto commandManager = DX12Rendering::RenderPassBlock::GetCurrentRenderPass()->GetCommandManager();
			//DX12Rendering::Commands::CommandManagerCycleBlock subCycleBlock(commandManager, "Depth Buffer Surface");
		}

		// get the expressions for conditionals / color / texcoords
		const float* regs = drawSurf->shaderRegisters;

		// if all stages of a material have been conditioned off, don't do anything
		int stage = 0;
		for (; stage < shader->GetNumStages(); stage++) {
			const shaderStage_t* pStage = shader->GetStage(stage);
			// check the stage enable condition
			if (regs[pStage->conditionRegister] != 0) {
				break;
			}
		}
		if (stage == shader->GetNumStages()) {
			continue;
		}

		// change the matrix if needed
		if (drawSurf->space != backEnd.currentSpace) {
			RB_SetMVP(drawSurf->space->mvp);

			backEnd.currentSpace = drawSurf->space;
		}

		uint64 surfGLState = 0;

		// set polygon offset if necessary
		if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
			surfGLState |= GLS_POLYGON_OFFSET;
			GL_PolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
		}

		// subviews will just down-modulate the color buffer
		float color[4];
		if (shader->GetSort() == SS_SUBVIEW) {
			surfGLState |= GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS;
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 1.0f;
		}
		else {
			// others just draw black
			color[0] = 0.0f;
			color[1] = 0.0f;
			color[2] = 0.0f;
			color[3] = 1.0f;
		}

		renderLog.OpenBlock(shader->GetName());

		bool drawSolid = false;
		if (shader->Coverage() == MC_OPAQUE) {
			drawSolid = true;
		}
		else if (shader->Coverage() == MC_PERFORATED) {
			// we may have multiple alpha tested stages
			// if the only alpha tested stages are condition register omitted,
			// draw a normal opaque surface
			bool didDraw = false;

			// perforated surfaces may have multiple alpha tested stages
			for (stage = 0; stage < shader->GetNumStages(); stage++) {
				const shaderStage_t* pStage = shader->GetStage(stage);

				if (!pStage->hasAlphaTest) {
					continue;
				}

				// check the stage enable condition
				if (regs[pStage->conditionRegister] == 0) {
					continue;
				}

				// if we at least tried to draw an alpha tested stage,
				// we won't draw the opaque surface
				didDraw = true;

				// set the alpha modulate
				color[3] = regs[pStage->color.registers[3]];

				// skip the entire stage if alpha would be black
				if (color[3] <= 0.0f) {
					continue;
				}

				uint64 stageGLState = surfGLState;

				// set privatePolygonOffset if necessary
				if (pStage->privatePolygonOffset) {
					GL_PolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
					stageGLState |= GLS_POLYGON_OFFSET;
				}

				GL_Color(color);

#ifdef USE_CORE_PROFILE
				GL_State(stageGLState);
				idVec4 alphaTestValue(regs[pStage->alphaTestRegister]);
				SetFragmentParm(RENDERPARM_ALPHA_TEST, alphaTestValue.ToFloatPtr());
#else
				GL_State(stageGLState | GLS_ALPHATEST_FUNC_GREATER | GLS_ALPHATEST_MAKE_REF(idMath::Ftob(255.0f * regs[pStage->alphaTestRegister])));
#endif

				if (drawSurf->jointCache) {
					renderProgManager.BindShader_TextureVertexColorSkinned();
				}
				else {
					renderProgManager.BindShader_TextureVertexColor();
				}
				RB_SetVertexColorParms(SVC_IGNORE);

				// bind the texture
				GL_SelectTexture(0);
				pStage->texture.image->Bind();

				// set texture matrix and texGens
				RB_PrepareStageTexturing(pStage, drawSurf);

				// must render with less-equal for Z-Cull to work properly
				assert((GL_GetCurrentState() & GLS_DEPTHFUNC_BITS) == GLS_DEPTHFUNC_LESS);

				// draw it
				RB_DrawElementsWithCounters(drawSurf);

				// clean up
				RB_FinishStageTexturing(pStage, drawSurf);

				// unset privatePolygonOffset if necessary
				if (pStage->privatePolygonOffset) {
					GL_PolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
				}
			}

			if (!didDraw) {
				drawSolid = true;
			}
		}

		// draw the entire surface solid
		if (drawSolid) {
			if (shader->GetSort() == SS_SUBVIEW) {
				renderProgManager.BindShader_Color();
				GL_Color(color);
				GL_State(surfGLState);
			}
			else {
				if (drawSurf->jointCache) {
					renderProgManager.BindShader_DepthSkinned();
				}
				else {
					renderProgManager.BindShader_Depth();
				}
				GL_State(surfGLState | GLS_ALPHAMASK);
			}

			// must render with less-equal for Z-Cull to work properly
			assert((GL_GetCurrentState() & GLS_DEPTHFUNC_BITS) == GLS_DEPTHFUNC_LESS);

			// draw it
			RB_DrawElementsWithCounters(drawSurf);
		}

		renderLog.CloseBlock();
	}

#ifdef USE_CORE_PROFILE
	SetFragmentParm(RENDERPARM_ALPHA_TEST, vec4_zero.ToFloatPtr());
#endif
}

/*
=====================
RB_FillDepthBufferFast

Optimized fast path code.

If there are subview surfaces, they must be guarded in the depth buffer to allow
the mirror / subview to show through underneath the current view rendering.

Surfaces with perforated shaders need the full shader setup done, but should be
drawn after the opaque surfaces.

The bulk of the surfaces should be simple opaque geometry that can be drawn very rapidly.

If there are no subview surfaces, we could clear to black and use fast-Z rendering
on the 360.
=====================
*/
static const DX12Rendering::Commands::FenceValue RB_FillDepthBufferFast(drawSurf_t** drawSurfs, int numDrawSurfs) {
	// TODO: Run this on it's own commandList so we can fill the rest while running.
	if (numDrawSurfs == 0) {
		return DX12Rendering::Commands::FenceValue(DX12Rendering::Commands::DIRECT, 0);
	}

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if (backEnd.viewDef->viewEntitys == NULL) {
		return DX12Rendering::Commands::FenceValue(DX12Rendering::Commands::DIRECT, 0);
	}

	renderLog.OpenMainBlock(MRB_FILL_DEPTH_BUFFER);
	renderLog.OpenBlock("RB_FillDepthBufferFast");

	DX12Rendering::RenderPassBlock renderPassBlock("DepthPass", DX12Rendering::Commands::DIRECT);

	GL_StartDepthPass(backEnd.viewDef->scissor);

	// force MVP change on first surface
	backEnd.currentSpace = NULL;

	// draw all the subview surfaces, which will already be at the start of the sorted list,
	// with the general purpose path
	GL_State(GLS_DEFAULT);

	int	surfNum;
	for (surfNum = 0; surfNum < numDrawSurfs; surfNum++) {
		if (drawSurfs[surfNum]->material->GetSort() != SS_SUBVIEW) {
			break;
		}
		RB_FillDepthBufferGeneric(&drawSurfs[surfNum], 1);
	}

	const drawSurf_t** perforatedSurfaces = (const drawSurf_t**)_alloca(numDrawSurfs * sizeof(drawSurf_t*));
	int numPerforatedSurfaces = 0;

	// draw all the opaque surfaces and build up a list of perforated surfaces that
	// we will defer drawing until all opaque surfaces are done
	GL_State(GLS_DEFAULT);

	// continue checking past the subview surfaces
	for (; surfNum < numDrawSurfs; surfNum++) {
		const drawSurf_t* surf = drawSurfs[surfNum];
		const idMaterial* shader = surf->material;

		// translucent surfaces don't put anything in the depth buffer
		if (shader->Coverage() == MC_TRANSLUCENT) {
			continue;
		}
		if (shader->Coverage() == MC_PERFORATED) {
			// save for later drawing
			perforatedSurfaces[numPerforatedSurfaces] = surf;
			numPerforatedSurfaces++;
			continue;
		}

		// set polygon offset?

		// set mvp matrix
		if (surf->space != backEnd.currentSpace) {
			RB_SetMVP(surf->space->mvp);
			backEnd.currentSpace = surf->space;
		}

		renderLog.OpenBlock(shader->GetName());

		if (surf->jointCache) {
			renderProgManager.BindShader_DepthSkinned();
		}
		else {
			renderProgManager.BindShader_Depth();
		}

		// must render with less-equal for Z-Cull to work properly
		assert((GL_GetCurrentState() & GLS_DEPTHFUNC_BITS) == GLS_DEPTHFUNC_LESS);

		// draw it solid
		RB_DrawElementsWithCounters(surf);

		renderLog.CloseBlock();
	}

	// draw all perforated surfaces with the general code path
	if (numPerforatedSurfaces > 0) {
		RB_FillDepthBufferGeneric(perforatedSurfaces, numPerforatedSurfaces);
	}

	// Allow platform specific data to be collected after the depth pass.
	GL_FinishDepthPass();

	auto commandList = renderPassBlock.GetCommandManager()->RequestNewCommandList();
	const DX12Rendering::Commands::FenceValue resultFence = commandList->AddPostFenceSignal();

	commandList->Close();

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();

	return resultFence;
}

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

const int INTERACTION_TEXUNIT_BUMP = 0;
const int INTERACTION_TEXUNIT_FALLOFF = 1;
const int INTERACTION_TEXUNIT_PROJECTION = 2;
const int INTERACTION_TEXUNIT_DIFFUSE = 3;
const int INTERACTION_TEXUNIT_SPECULAR = 4;
const int INTERACTION_TEXUINIT_SHADOW = 5;

/*
==================
RB_SetupInteractionStage
==================
*/
static void RB_SetupInteractionStage(const shaderStage_t* surfaceStage, const float* surfaceRegs, const float lightColor[4],
	idVec4 matrix[2], float color[4]) {

	if (surfaceStage->texture.hasMatrix) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0.0f;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0.0f;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large texture values, but
		// center rotations and center scales can still generate offsets that need to be > 1
		if (matrix[0][3] < -40.0f || matrix[0][3] > 40.0f) {
			matrix[0][3] -= idMath::Ftoi(matrix[0][3]);
		}
		if (matrix[1][3] < -40.0f || matrix[1][3] > 40.0f) {
			matrix[1][3] -= idMath::Ftoi(matrix[1][3]);
		}
	}
	else {
		matrix[0][0] = 1.0f;
		matrix[0][1] = 0.0f;
		matrix[0][2] = 0.0f;
		matrix[0][3] = 0.0f;

		matrix[1][0] = 0.0f;
		matrix[1][1] = 1.0f;
		matrix[1][2] = 0.0f;
		matrix[1][3] = 0.0f;
	}

	if (color != NULL) {
		for (int i = 0; i < 4; i++) {
			// clamp here, so cards with a greater range don't look different.
			// we could perform overbrighting like we do for lights, but
			// it doesn't currently look worth it.
			color[i] = idMath::ClampFloat(0.0f, 1.0f, surfaceRegs[surfaceStage->color.registers[i]]) * lightColor[i];
		}
	}
}

/*
=================
RB_DrawSingleInteraction
=================
*/
static void RB_DrawSingleInteraction(drawInteraction_t* din) {
	if (din->bumpImage == NULL) {
		// stage wasn't actually an interaction
		return;
	}
	
	if (din->diffuseImage == NULL || r_skipDiffuse.GetBool()) {
		// this isn't a YCoCg black, but it doesn't matter, because
		// the diffuseColor will also be 0
		din->diffuseImage = globalImages->blackImage;
	}
	if (din->specularImage == NULL || r_skipSpecular.GetBool() || din->ambientLight) {
		din->specularImage = globalImages->blackImage;
	}
	if (r_skipBump.GetBool()) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	const bool diffuseIsBlack = (din->diffuseImage == globalImages->blackImage)
		|| ((din->diffuseColor[0] <= 0) && (din->diffuseColor[1] <= 0) && (din->diffuseColor[2] <= 0));
	const bool specularIsBlack = (din->specularImage == globalImages->blackImage)
		|| ((din->specularColor[0] <= 0) && (din->specularColor[1] <= 0) && (din->specularColor[2] <= 0));
	if (diffuseIsBlack && specularIsBlack) {
		return;
	}

	// bump matrix
	SetVertexParm(RENDERPARM_BUMPMATRIX_S, din->bumpMatrix[0].ToFloatPtr());
	SetVertexParm(RENDERPARM_BUMPMATRIX_T, din->bumpMatrix[1].ToFloatPtr());

	// diffuse matrix
	SetVertexParm(RENDERPARM_DIFFUSEMATRIX_S, din->diffuseMatrix[0].ToFloatPtr());
	SetVertexParm(RENDERPARM_DIFFUSEMATRIX_T, din->diffuseMatrix[1].ToFloatPtr());

	// specular matrix
	SetVertexParm(RENDERPARM_SPECULARMATRIX_S, din->specularMatrix[0].ToFloatPtr());
	SetVertexParm(RENDERPARM_SPECULARMATRIX_T, din->specularMatrix[1].ToFloatPtr());

	RB_SetVertexColorParms(din->vertexColor);

	SetFragmentParm(RENDERPARM_DIFFUSEMODIFIER, din->diffuseColor.ToFloatPtr());
	SetFragmentParm(RENDERPARM_SPECULARMODIFIER, din->specularColor.ToFloatPtr());

	// texture 0 will be the per-surface bump map
	GL_SelectTexture(INTERACTION_TEXUNIT_BUMP);
	din->bumpImage->Bind();

	// texture 3 is the per-surface diffuse map
	GL_SelectTexture(INTERACTION_TEXUNIT_DIFFUSE);
	din->diffuseImage->Bind();

	// texture 4 is the per-surface specular map
	GL_SelectTexture(INTERACTION_TEXUNIT_SPECULAR);
	din->specularImage->Bind();

	// texture 5 is the screenspace shadow texture.
	GL_SelectTexture(INTERACTION_TEXUINIT_SHADOW);
	dxRenderer.SetTexture(DX12Rendering::GetTextureManager()->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SHADOWMAP));

	RB_DrawElementsWithCounters(din->surf);
}

/*
=============
GL_BlockingSwapBuffers

We want to exit this with the GPU idle, right at vsync
=============
*/
const void GL_BlockingSwapBuffers() {
	RENDERLOG_PRINTF("***************** GL_BlockingSwapBuffers *****************\n\n\n");

	const int beforeFinish = Sys_Milliseconds();

	// TODO: Implement

	/*if (!glConfig.syncAvailable) {
		glFinish();
	}

	const int beforeSwap = Sys_Milliseconds();
	if (r_showSwapBuffers.GetBool() && beforeSwap - beforeFinish > 1) {
		common->Printf("%i msec to glFinish\n", beforeSwap - beforeFinish);
	}*/

	// TODO: Check if this will be an issue.
	//GLimp_SwapBuffers();

	const int beforeFence = Sys_Milliseconds();
	/*if (r_showSwapBuffers.GetBool() && beforeFence - beforeSwap > 1) {
		common->Printf("%i msec to swapBuffers\n", beforeFence - beforeSwap);
	}*/

	//if (glConfig.syncAvailable) {
	//	swapIndex ^= 1;

	//	if (qglIsSync(renderSync[swapIndex])) {
	//		qglDeleteSync(renderSync[swapIndex]);
	//	}
	//	// draw something tiny to ensure the sync is after the swap
	//	const int start = Sys_Milliseconds();
	//	qglScissor(0, 0, 1, 1);
	//	qglEnable(GL_SCISSOR_TEST);
	//	qglClear(GL_COLOR_BUFFER_BIT);
	//	renderSync[swapIndex] = qglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	//	const int end = Sys_Milliseconds();
	//	if (r_showSwapBuffers.GetBool() && end - start > 1) {
	//		common->Printf("%i msec to start fence\n", end - start);
	//	}

	//	GLsync	syncToWaitOn;
	//	if (r_syncEveryFrame.GetBool()) {
	//		syncToWaitOn = renderSync[swapIndex];
	//	}
	//	else {
	//		syncToWaitOn = renderSync[!swapIndex];
	//	}

	//	if (qglIsSync(syncToWaitOn)) {
	//		for (GLenum r = GL_TIMEOUT_EXPIRED; r == GL_TIMEOUT_EXPIRED; ) {
	//			r = qglClientWaitSync(syncToWaitOn, GL_SYNC_FLUSH_COMMANDS_BIT, 1000 * 1000);
	//		}
	//	}
	//}

	const int afterFence = Sys_Milliseconds();
	/*if (r_showSwapBuffers.GetBool() && afterFence - beforeFence > 1) {
		common->Printf("%i msec to wait on fence\n", afterFence - beforeFence);
	}*/

	const int64 exitBlockTime = Sys_Microseconds();

	static int64 prevBlockTime;
	/*if (r_showSwapBuffers.GetBool() && prevBlockTime) {
		const int delta = (int)(exitBlockTime - prevBlockTime);
		common->Printf("blockToBlock: %i\n", delta);
	}*/
	prevBlockTime = exitBlockTime;
}

/*
======================
RB_BindVariableStageImage

Handles generating a cinematic frame if needed
======================
*/
static void RB_BindVariableStageImage(const textureStage_t* texture, const float* shaderRegisters) {
	if (texture->cinematic) {
		cinData_t cin;

		if (r_skipDynamicTextures.GetBool()) {
			globalImages->defaultImage->Bind();
			return;
		}

		// offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
		// We make no attempt to optimize for multiple identical cinematics being in view, or
		// for cinematics going at a lower framerate than the renderer.
		cin = texture->cinematic->ImageForTime(backEnd.viewDef->renderView.time[0] + idMath::Ftoi(1000.0f * backEnd.viewDef->renderView.shaderParms[11]));
		if (cin.imageY != NULL) {
			GL_SelectTexture(0);
			cin.imageY->Bind();
			GL_SelectTexture(1);
			cin.imageCr->Bind();
			GL_SelectTexture(2);
			cin.imageCb->Bind();
		}
		else {
			globalImages->blackImage->Bind();
			// because the shaders may have already been set - we need to make sure we are not using a bink shader which would 
			// display incorrectly.  We may want to get rid of RB_BindVariableStageImage and inline the code so that the
			// SWF GUI case is handled better, too
			renderProgManager.BindShader_TextureVertexColor(); // TODO: Evaluate
		}
	}
	else {
		// FIXME: see why image is invalid
		if (texture->image != NULL) {
			texture->image->Bind();
		}
	}
}

/*
=====================
RB_StencilShadowPass

The stencil buffer should have been set to 128 on any surfaces that might receive shadows.
=====================
*/
static void RB_StencilShadowPass(const drawSurf_t* drawSurfs, const viewLight_t* vLight) {
	if (r_skipShadows.GetBool()) {
		return;
	}
	
	if (drawSurfs == NULL) {
		return;
	}
	
	auto commandManager = DX12Rendering::RenderPassBlock::GetCurrentRenderPass()->GetCommandManager();
	DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RB_StencilShadowPass");

	RENDERLOG_PRINTF("---------- RB_StencilShadowPass ----------\n");

	renderProgManager.BindShader_Shadow();

	GL_SelectTexture(0);
	globalImages->BindNull();

	uint64 glState = 0;
	DX12Rendering::eSurfaceVariant variant = DX12Rendering::VARIANT_DEFAULT;
	size_t vertexSize = sizeof(idDrawVert);

	// for visualizing the shadows
	if (r_showShadows.GetInteger()) {
		// set the debug shadow color
		SetFragmentParm(RENDERPARM_COLOR, colorMagenta.ToFloatPtr());
		if (r_showShadows.GetInteger() == 2) {
			// draw filled in
			glState = GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS;
		}
		else {
			// draw as lines, filling the depth buffer
			glState = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS;
		}
	}
	else {
		// don't write to the color or depth buffer, just the stencil buffer
		glState = GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS;
	}
	
	// Implemented in DX12_ApplyPSOVariant instead.
	GL_PolygonOffset(r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat());

	// the actual stencil func will be set in the draw code, but we need to make sure it isn't
	// disabled here, and that the value will get reset for the interactions without looking
	// like a no-change-required
	GL_State(glState | GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_INCR |
		GLS_STENCIL_MAKE_REF(STENCIL_SHADOW_TEST_VALUE) | GLS_STENCIL_MAKE_MASK(STENCIL_SHADOW_MASK_VALUE) | GLS_POLYGON_OFFSET);

	//// Two Sided Stencil reduces two draw calls to one for slightly faster shadows
	GL_Cull(CT_TWO_SIDED);


	//// process the chain of shadows with the current rendering state
	backEnd.currentSpace = NULL;

	for (const drawSurf_t* drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight) {
		if (drawSurf->scissorRect.IsEmpty()) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		// make sure the shadow volume is done
		if (drawSurf->shadowVolumeState != SHADOWVOLUME_DONE) {
			assert(drawSurf->shadowVolumeState == SHADOWVOLUME_UNFINISHED || drawSurf->shadowVolumeState == SHADOWVOLUME_DONE);
			
			uint64 start = Sys_Microseconds();
			while (drawSurf->shadowVolumeState == SHADOWVOLUME_UNFINISHED) {
				Sys_Yield();
			}
			uint64 end = Sys_Microseconds();

			backEnd.pc.shadowMicroSec += end - start;
		}

		if (drawSurf->numIndexes == 0) {
			continue;	// a job may have created an empty shadow volume
		}

		if (!backEnd.currentScissor.Equals(drawSurf->scissorRect) && r_useScissor.GetBool()) {
			// change the scissor
			GL_Scissor(backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
				backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
				drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
				drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1);
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if (drawSurf->space != backEnd.currentSpace) {
			// change the matrix
			RB_SetMVP(drawSurf->space->mvp);

			// set the local light position to allow the vertex program to project the shadow volume end cap to infinity
			idVec4 localLight(0.0f);
			R_GlobalPointToLocal(drawSurf->space->modelMatrix, vLight->globalLightOrigin, localLight.ToVec3());
			SetVertexParm(RENDERPARM_LOCALLIGHTORIGIN, localLight.ToFloatPtr());

			backEnd.currentSpace = drawSurf->space;
		}

		if (r_showShadows.GetInteger() == 0) {
			if (drawSurf->jointCache) {
				renderProgManager.BindShader_ShadowSkinned();
			}
			else {
				renderProgManager.BindShader_Shadow();
			}
		}
		else {
			if (drawSurf->jointCache) {
				renderProgManager.BindShader_ShadowDebugSkinned();
			}
			else {
				renderProgManager.BindShader_ShadowDebug();
			}
		}

		// set depth bounds per shadow
		if (r_useShadowDepthBounds.GetBool()) {
			GL_DepthBoundsTest(drawSurf->scissorRect.zmin, drawSurf->scissorRect.zmax);
		}

		// Determine whether or not the shadow volume needs to be rendered with Z-pass or
		// Z-fail. It is worthwhile to spend significant resources to reduce the number of
		// cases where shadow volumes need to be rendered with Z-fail because Z-fail
		// rendering can be significantly slower even on today's hardware. For instance,
		// on NVIDIA hardware Z-fail rendering causes the Z-Cull to be used in reverse:
		// Z-near becomes Z-far (trivial accept becomes trivial reject). Using the Z-Cull
		// in reverse is far less efficient because the Z-Cull only stores Z-near per 16x16
		// pixels while the Z-far is stored per 4x2 pixels. (The Z-near coallesce buffer
		// which has 4x4 granularity is only used when updating the depth which is not the
		// case for shadow volumes.) Note that it is also important to NOT use a Z-Cull
		// reconstruct because that would clear the Z-near of the Z-Cull which results in
		// no trivial rejection for Z-fail stencil shadow rendering.

		const bool renderZPass = (drawSurf->renderZFail == 0) || r_forceZPassStencilShadows.GetBool();

		if (renderZPass) {
			// Z-pass
			variant = DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS;
		}
		else if (r_useStencilShadowPreload.GetBool()) {
			// preload + Z-pass
			variant = DX12Rendering::VARIANT_STENCIL_SHADOW_STENCILSHADOWPRELOAD;
		}
		else {
			// Z-fail
		}

		if (drawSurf->jointCache) {
			variant = static_cast<DX12Rendering::eSurfaceVariant>(variant + 1); // Skinned variant.
			vertexSize = sizeof(idShadowVertSkinned);
			backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT_SKINNED;
		}
		else{
			vertexSize = sizeof(idShadowVert);
			backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT;
		}

		RB_DrawElementsWithCounters(drawSurf, drawSurf->shadowCache, vertexSize, variant, nullptr, 0);

		if (!renderZPass && r_useStencilShadowPreload.GetBool())
		{
			variant = drawSurf->jointCache ? DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS_SKINNED : DX12Rendering::VARIANT_STENCIL_SHADOW_RENDER_ZPASS;
			// Render again with z-pass
			RB_DrawElementsWithCounters(drawSurf, drawSurf->shadowCache, vertexSize, variant, nullptr, 0);
		}

		//// get vertex buffer
		//const vertCacheHandle_t vbHandle = drawSurf->shadowCache;
		//idVertexBuffer* vertexBuffer;
		//if (vertexCache.CacheIsStatic(vbHandle)) {
		//	vertexBuffer = &vertexCache.staticData.vertexBuffer;
		//}
		//else {
		//	const uint64 frameNum = (int)(vbHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
		//	if (frameNum != ((vertexCache.currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
		//		idLib::Warning("RB_DrawElementsWithCounters, vertexBuffer == NULL");
		//		continue;
		//	}
		//	vertexBuffer = &vertexCache.frameData[vertexCache.drawListNum].vertexBuffer;
		//}
		//const int vertOffset = (int)(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

		//// get index buffer
		//const vertCacheHandle_t ibHandle = drawSurf->indexCache;
		//idIndexBuffer* indexBuffer;
		//if (vertexCache.CacheIsStatic(ibHandle)) {
		//	indexBuffer = &vertexCache.staticData.indexBuffer;
		//}
		//else {
		//	const uint64 frameNum = (int)(ibHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
		//	if (frameNum != ((vertexCache.currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
		//		idLib::Warning("RB_DrawElementsWithCounters, indexBuffer == NULL");
		//		continue;
		//	}
		//	indexBuffer = &vertexCache.frameData[vertexCache.drawListNum].indexBuffer;
		//}
		//const uint64 indexOffset = (int)(ibHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

		//RENDERLOG_PRINTF("Binding Buffers: %p %p\n", vertexBuffer, indexBuffer);


		//if (backEnd.glState.currentIndexBuffer != (GLuint)indexBuffer->GetAPIObject() || !r_useStateCaching.GetBool()) {
		//	qglBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, (GLuint)indexBuffer->GetAPIObject());
		//	backEnd.glState.currentIndexBuffer = (GLuint)indexBuffer->GetAPIObject();
		//}

		//if (drawSurf->jointCache) {
		//	assert(renderProgManager.ShaderUsesJoints());

		//	idJointBuffer jointBuffer;
		//	if (!vertexCache.GetJointBuffer(drawSurf->jointCache, &jointBuffer)) {
		//		idLib::Warning("RB_DrawElementsWithCounters, jointBuffer == NULL");
		//		continue;
		//	}
		//	assert((jointBuffer.GetOffset() & (glConfig.uniformBufferOffsetAlignment - 1)) == 0);

		//	const GLuint ubo = reinterpret_cast<GLuint>(jointBuffer.GetAPIObject());
		//	qglBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, jointBuffer.GetOffset(), jointBuffer.GetNumJoints() * sizeof(idJointMat));

		//	if ((backEnd.glState.vertexLayout != LAYOUT_DRAW_SHADOW_VERT_SKINNED) || (backEnd.glState.currentVertexBuffer != (GLuint)vertexBuffer->GetAPIObject()) || !r_useStateCaching.GetBool()) {
		//		qglBindBufferARB(GL_ARRAY_BUFFER_ARB, (GLuint)vertexBuffer->GetAPIObject());
		//		backEnd.glState.currentVertexBuffer = (GLuint)vertexBuffer->GetAPIObject();

		//		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_VERTEX);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_NORMAL);
		//		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_COLOR);
		//		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_COLOR2);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_ST);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_TANGENT);

		//		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_VERTEX, 4, GL_FLOAT, GL_FALSE, sizeof(idShadowVertSkinned), (void*)(SHADOWVERTSKINNED_XYZW_OFFSET));
		//		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(idShadowVertSkinned), (void*)(SHADOWVERTSKINNED_COLOR_OFFSET));
		//		qglVertexAttribPointerARB(PC_ATTRIB_INDEX_COLOR2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(idShadowVertSkinned), (void*)(SHADOWVERTSKINNED_COLOR2_OFFSET));

		//		backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT_SKINNED;
		//	}

		//}
		//else {

		//	if ((backEnd.glState.vertexLayout != LAYOUT_DRAW_SHADOW_VERT) || (backEnd.glState.currentVertexBuffer != (GLuint)vertexBuffer->GetAPIObject()) || !r_useStateCaching.GetBool()) {
		//		qglBindBufferARB(GL_ARRAY_BUFFER_ARB, (GLuint)vertexBuffer->GetAPIObject());
		//		backEnd.glState.currentVertexBuffer = (GLuint)vertexBuffer->GetAPIObject();

		//		qglEnableVertexAttribArrayARB(PC_ATTRIB_INDEX_VERTEX);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_NORMAL);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_COLOR);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_COLOR2);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_ST);
		//		qglDisableVertexAttribArrayARB(PC_ATTRIB_INDEX_TANGENT);

				//qglVertexAttribPointerARB(PC_ATTRIB_INDEX_VERTEX, 4, GL_FLOAT, GL_FALSE, sizeof(idShadowVert), (void*)(SHADOWVERT_XYZW_OFFSET));

		//		backEnd.glState.vertexLayout = LAYOUT_DRAW_SHADOW_VERT;
		//	}
		//}

		//renderProgManager.CommitUniforms();

		//if (drawSurf->jointCache) {
		//	qglDrawElementsBaseVertex(GL_TRIANGLES, r_singleTriangle.GetBool() ? 3 : drawSurf->numIndexes, GL_INDEX_TYPE, (triIndex_t*)indexOffset, vertOffset / sizeof(idShadowVertSkinned));
		//}
		//else {
		//	qglDrawElementsBaseVertex(GL_TRIANGLES, r_singleTriangle.GetBool() ? 3 : drawSurf->numIndexes, GL_INDEX_TYPE, (triIndex_t*)indexOffset, vertOffset / sizeof(idShadowVert));
		//}

		//if (!renderZPass && r_useStencilShadowPreload.GetBool()) {
		//	// render again with Z-pass
		//	qglStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR);
		//	qglStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR);

		//	if (drawSurf->jointCache) {
		//		qglDrawElementsBaseVertex(GL_TRIANGLES, r_singleTriangle.GetBool() ? 3 : drawSurf->numIndexes, GL_INDEX_TYPE, (triIndex_t*)indexOffset, vertOffset / sizeof(idShadowVertSkinned));
		//	}
		//	else {
		//		qglDrawElementsBaseVertex(GL_TRIANGLES, r_singleTriangle.GetBool() ? 3 : drawSurf->numIndexes, GL_INDEX_TYPE, (triIndex_t*)indexOffset, vertOffset / sizeof(idShadowVert));
		//	}
		//}
	}

	// cleanup the shadow specific rendering state

	GL_Cull(CT_FRONT_SIDED);

	// reset depth bounds
	if (r_useShadowDepthBounds.GetBool()) {
		if (r_useLightDepthBounds.GetBool()) {
			GL_DepthBoundsTest(vLight->scissorRect.zmin, vLight->scissorRect.zmax);
		}
		else {
			GL_DepthBoundsTest(0.0f, 0.0f);
		}
	}
}

/*
==================
RB_StencilSelectLight

Deform the zeroOneCubeModel to exactly cover the light volume. Render the deformed cube model to the stencil buffer in
such a way that only fragments that are directly visible and contained within the volume will be written creating a
mask to be used by the following stencil shadow and draw interaction passes.
==================
*/
static void RB_StencilSelectLight(const viewLight_t* vLight) {
	// TODO: Implement
	renderLog.OpenBlock("Stencil Select");

	auto commandManager = DX12Rendering::RenderPassBlock::GetCurrentRenderPass()->GetCommandManager();
	DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RB_StencilSelectLight");

	// enable the light scissor
	if (!backEnd.currentScissor.Equals(vLight->scissorRect) && r_useScissor.GetBool()) {
		GL_Scissor(backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1,
			backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
			vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
			vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1);
		backEnd.currentScissor = vLight->scissorRect;
	}

	//// clear stencil buffer to 0 (not drawable)
	uint64 glStateMinusStencil = GL_GetCurrentStateMinusStencil();
	GL_State(glStateMinusStencil | GLS_STENCIL_FUNC_ALWAYS | GLS_STENCIL_MAKE_REF(STENCIL_SHADOW_TEST_VALUE) | GLS_STENCIL_MAKE_MASK(STENCIL_SHADOW_MASK_VALUE));	// make sure stencil mask passes for the clear
	GL_Clear(false, false, true, 0, 0.0f, 0.0f, 0.0f, 0.0f);	// clear to 0 for stencil select
	//TODO: Figure out why this clear value of 128 works instead of 0.

	//// set the depthbounds
	GL_DepthBoundsTest(vLight->scissorRect.zmin, vLight->scissorRect.zmax);


	GL_State(GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHMASK | GLS_DEPTHFUNC_LESS | GLS_STENCIL_FUNC_ALWAYS | GLS_STENCIL_MAKE_REF(STENCIL_SHADOW_TEST_VALUE) | GLS_STENCIL_MAKE_MASK(STENCIL_SHADOW_MASK_VALUE));
	GL_Cull(CT_TWO_SIDED);

	renderProgManager.BindShader_Depth();

	//// set the matrix for deforming the 'zeroOneCubeModel' into the frustum to exactly cover the light volume
	idRenderMatrix invProjectMVPMatrix;
	idRenderMatrix::Multiply(backEnd.viewDef->worldSpace.mvp, vLight->inverseBaseLightProject, invProjectMVPMatrix);
	RB_SetMVP(invProjectMVPMatrix);

	//// two-sided stencil test
	const DX12Rendering::eSurfaceVariant variant = DX12Rendering::VARIANT_STENCIL_TWOSIDED;

	RB_DrawElementsWithCounters(&backEnd.zeroOneCubeSurface, backEnd.zeroOneCubeSurface.ambientCache, 0, variant, nullptr, 0);

	//// reset stencil state

	GL_Cull(CT_FRONT_SIDED);

	renderProgManager.Unbind();

	//// unset the depthbounds
	GL_DepthBoundsTest(0.0f, 0.0f);

	renderLog.CloseBlock();
}

/*
=================
RB_SetupForFastPathInteractions

These are common for all fast path surfaces
=================
*/
static void RB_SetupForFastPathInteractions(const idVec4& diffuseColor, const idVec4& specularColor) {
	const idVec4 sMatrix(1, 0, 0, 0);
	const idVec4 tMatrix(0, 1, 0, 0);

	// bump matrix
	SetVertexParm(RENDERPARM_BUMPMATRIX_S, sMatrix.ToFloatPtr());
	SetVertexParm(RENDERPARM_BUMPMATRIX_T, tMatrix.ToFloatPtr());

	// diffuse matrix
	SetVertexParm(RENDERPARM_DIFFUSEMATRIX_S, sMatrix.ToFloatPtr());
	SetVertexParm(RENDERPARM_DIFFUSEMATRIX_T, tMatrix.ToFloatPtr());

	// specular matrix
	SetVertexParm(RENDERPARM_SPECULARMATRIX_S, sMatrix.ToFloatPtr());
	SetVertexParm(RENDERPARM_SPECULARMATRIX_T, tMatrix.ToFloatPtr());

	RB_SetVertexColorParms(SVC_IGNORE);

	SetFragmentParm(RENDERPARM_DIFFUSEMODIFIER, diffuseColor.ToFloatPtr());
	SetFragmentParm(RENDERPARM_SPECULARMODIFIER, specularColor.ToFloatPtr());
}

/*
=============
RB_RenderInteractions

With added sorting and trivial path work.
=============
*/
static void RB_RenderInteractions(const drawSurf_t* surfList, const viewLight_t* vLight, int depthFunc, bool performStencilTest, bool useLightDepthBounds) {
	if (surfList == NULL) {
		return;
	}

	// change the scissor if needed, it will be constant across all the surfaces lit by the light
	if (!backEnd.currentScissor.Equals(vLight->scissorRect) && r_useScissor.GetBool()) {
		GL_Scissor(backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1,
			backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
			vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
			vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1);
		backEnd.currentScissor = vLight->scissorRect;
	}

	// perform setup here that will be constant for all interactions
	if (performStencilTest) {
		GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc | GLS_STENCIL_FUNC_EQUAL | GLS_STENCIL_MAKE_REF(STENCIL_SHADOW_TEST_VALUE) | GLS_STENCIL_MAKE_MASK(STENCIL_SHADOW_MASK_VALUE));

	}
	else {
		GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc | GLS_STENCIL_FUNC_ALWAYS);
	}

#ifdef _DEBUG
	dxRenderer.DebugAddLight(*vLight);
#endif

	// some rare lights have multiple animating stages, loop over them outside the surface list
	const idMaterial* lightShader = vLight->lightShader;
	const float* lightRegs = vLight->shaderRegisters;

	drawInteraction_t inter = {};
	inter.ambientLight = lightShader->IsAmbientLight();

	//---------------------------------
	// Split out the complex surfaces from the fast-path surfaces
	// so we can do the fast path ones all in a row.
	// The surfaces should already be sorted by space because they
	// are added single-threaded, and there is only a negligable amount
	// of benefit to trying to sort by materials.
	//---------------------------------
	static const int MAX_INTERACTIONS_PER_LIGHT = 1024;
	static const int MAX_COMPLEX_INTERACTIONS_PER_LIGHT = 128;
	idStaticList< const drawSurf_t*, MAX_INTERACTIONS_PER_LIGHT > allSurfaces;
	idStaticList< const drawSurf_t*, MAX_COMPLEX_INTERACTIONS_PER_LIGHT > complexSurfaces;
	for (const drawSurf_t* walk = surfList; walk != NULL; walk = walk->nextOnLight) {

		// make sure the triangle culling is done
		if (walk->shadowVolumeState != SHADOWVOLUME_DONE) {
			assert(walk->shadowVolumeState == SHADOWVOLUME_UNFINISHED || walk->shadowVolumeState == SHADOWVOLUME_DONE);

			uint64 start = Sys_Microseconds();
			while (walk->shadowVolumeState == SHADOWVOLUME_UNFINISHED) {
				Sys_Yield();
			}
			uint64 end = Sys_Microseconds();

			backEnd.pc.shadowMicroSec += end - start;
		}

		const idMaterial* surfaceShader = walk->material;
		if (surfaceShader->GetFastPathBumpImage()) {
			allSurfaces.Append(walk);
		}
		else {
			complexSurfaces.Append(walk);
		}
	}
	for (int i = 0; i < complexSurfaces.Num(); i++) {
		allSurfaces.Append(complexSurfaces[i]);
	}

	bool lightDepthBoundsDisabled = false;

	for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++) {
		const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);

		// ignore stages that fail the condition
		if (!lightRegs[lightStage->conditionRegister]) {
			continue;
		}

		const float lightScale = r_lightScale.GetFloat();
		const idVec4 lightColor(
			lightScale * lightRegs[lightStage->color.registers[0]],
			lightScale * lightRegs[lightStage->color.registers[1]],
			lightScale * lightRegs[lightStage->color.registers[2]],
			lightRegs[lightStage->color.registers[3]]);
		// apply the world-global overbright and the 2x factor for specular
		const idVec4 diffuseColor = lightColor;
		const idVec4 specularColor = lightColor * 2.0f;

		float lightTextureMatrix[16];
		if (lightStage->texture.hasMatrix) {
			RB_GetShaderTextureMatrix(lightRegs, &lightStage->texture, lightTextureMatrix);
		}

		// texture 1 will be the light falloff texture
		GL_SelectTexture(INTERACTION_TEXUNIT_FALLOFF);
		vLight->falloffImage->Bind();

		// texture 2 will be the light projection texture
		GL_SelectTexture(INTERACTION_TEXUNIT_PROJECTION);
		lightStage->texture.image->Bind();

		// force the light textures to not use anisotropic filtering, which is wasted on them
		// all of the texture sampler parms should be constant for all interactions, only
		// the actual texture image bindings will change

		//----------------------------------
		// For all surfaces on this light list, generate an interaction for this light stage
		//----------------------------------

		// setup renderparms assuming we will be drawing trivial surfaces first
		RB_SetupForFastPathInteractions(diffuseColor, specularColor);

		// even if the space does not change between light stages, each light stage may need a different lightTextureMatrix baked in
		backEnd.currentSpace = NULL;

		for (int sortedSurfNum = 0; sortedSurfNum < allSurfaces.Num(); sortedSurfNum++) {
			const drawSurf_t* const surf = allSurfaces[sortedSurfNum];

			// select the render prog
			if (lightShader->IsAmbientLight()) {
				if (surf->jointCache) {
					renderProgManager.BindShader_InteractionAmbientSkinned();
				}
				else {
					renderProgManager.BindShader_InteractionAmbient();
				}
			}
			else {
				if (surf->jointCache) {
					renderProgManager.BindShader_InteractionSkinned();
				}
				else {
					renderProgManager.BindShader_Interaction();
				}
			}

			const idMaterial* surfaceShader = surf->material;
			const float* surfaceRegs = surf->shaderRegisters;

			inter.surf = surf;

			// change the MVP matrix, view/light origin and light projection vectors if needed
			if (surf->space != backEnd.currentSpace) {
				backEnd.currentSpace = surf->space;

				// turn off the light depth bounds test if this model is rendered with a depth hack
				if (useLightDepthBounds) {
					if (!surf->space->weaponDepthHack && surf->space->modelDepthHack == 0.0f) {
						if (lightDepthBoundsDisabled) {
							GL_DepthBoundsTest(vLight->scissorRect.zmin, vLight->scissorRect.zmax);
							lightDepthBoundsDisabled = false;
						}
					}
					else {
						if (!lightDepthBoundsDisabled) {
							GL_DepthBoundsTest(0.0f, 0.0f);
							lightDepthBoundsDisabled = true;
						}
					}
				}

				// model-view-projection
				RB_SetMVP(surf->space->mvp);

				// tranform the light/view origin into model local space
				idVec4 localLightOrigin(0.0f);
				idVec4 localViewOrigin(1.0f);
				R_GlobalPointToLocal(&surf->space->modelMatrix[0], vLight->globalLightOrigin, localLightOrigin.ToVec3());
				R_GlobalPointToLocal(&surf->space->modelMatrix[0], backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3());

				// set the local light/view origin
				SetVertexParm(RENDERPARM_LOCALLIGHTORIGIN, localLightOrigin.ToFloatPtr());
				SetVertexParm(RENDERPARM_LOCALVIEWORIGIN, localViewOrigin.ToFloatPtr());

				// transform the light project into model local space
				idPlane lightProjection[4];
				for (int i = 0; i < 4; i++) {
					R_GlobalPlaneToLocal(surf->space->modelMatrix, vLight->lightProject[i], lightProjection[i]);
				}

				// optionally multiply the local light projection by the light texture matrix
				if (lightStage->texture.hasMatrix) {
					RB_BakeTextureMatrixIntoTexgen(lightProjection, lightTextureMatrix);
				}

				// set the light projection
				SetVertexParm(RENDERPARM_LIGHTPROJECTION_S, lightProjection[0].ToFloatPtr());
				SetVertexParm(RENDERPARM_LIGHTPROJECTION_T, lightProjection[1].ToFloatPtr());
				SetVertexParm(RENDERPARM_LIGHTPROJECTION_Q, lightProjection[2].ToFloatPtr());
				SetVertexParm(RENDERPARM_LIGHTFALLOFF_S, lightProjection[3].ToFloatPtr());
			}

			// check for the fast path
			if (surfaceShader->GetFastPathBumpImage() && !r_skipInteractionFastPath.GetBool()) {
				renderLog.OpenBlock(surf->material->GetName());

				// texture 0 will be the per-surface bump map
				GL_SelectTexture(INTERACTION_TEXUNIT_BUMP);
				surfaceShader->GetFastPathBumpImage()->Bind();

				// texture 3 is the per-surface diffuse map
				GL_SelectTexture(INTERACTION_TEXUNIT_DIFFUSE);
				surfaceShader->GetFastPathDiffuseImage()->Bind();

				// texture 4 is the per-surface specular map
				GL_SelectTexture(INTERACTION_TEXUNIT_SPECULAR);
				surfaceShader->GetFastPathSpecularImage()->Bind();

				// texture 5 is the screenspace shadow texture.
				GL_SelectTexture(INTERACTION_TEXUINIT_SHADOW);
				dxRenderer.SetTexture(DX12Rendering::GetTextureManager()->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SHADOWMAP));

				RB_DrawElementsWithCounters(surf);

				renderLog.CloseBlock();
				continue;
			}

			renderLog.OpenBlock(surf->material->GetName());

			inter.bumpImage = NULL;
			inter.specularImage = NULL;
			inter.diffuseImage = NULL;
			inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
			inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

			// go through the individual surface stages
			//
			// This is somewhat arcane because of the old support for video cards that had to render
			// interactions in multiple passes.
			//
			// We also have the very rare case of some materials that have conditional interactions
			// for the "hell writing" that can be shined on them.
			for (int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++) {
				const shaderStage_t* surfaceStage = surfaceShader->GetStage(surfaceStageNum);

				switch (surfaceStage->lighting) {
				case SL_COVERAGE: {
					// ignore any coverage stages since they should only be used for the depth fill pass
					// for diffuse stages that use alpha test.
					break;
				}
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					// draw any previous interaction
					if (inter.bumpImage != NULL) {
						RB_DrawSingleInteraction(&inter);
					}
					inter.bumpImage = surfaceStage->texture.image;
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					RB_SetupInteractionStage(surfaceStage, surfaceRegs, NULL,
						inter.bumpMatrix, NULL);
					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					// draw any previous interaction
					if (inter.diffuseImage != NULL) {
						RB_DrawSingleInteraction(&inter);
					}
					inter.diffuseImage = surfaceStage->texture.image;
					inter.vertexColor = surfaceStage->vertexColor;
					RB_SetupInteractionStage(surfaceStage, surfaceRegs, diffuseColor.ToFloatPtr(),
						inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr());
					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					// draw any previous interaction
					if (inter.specularImage != NULL) {
						RB_DrawSingleInteraction(&inter);
					}
					inter.specularImage = surfaceStage->texture.image;
					inter.vertexColor = surfaceStage->vertexColor;
					RB_SetupInteractionStage(surfaceStage, surfaceRegs, specularColor.ToFloatPtr(),
						inter.specularMatrix, inter.specularColor.ToFloatPtr());
					break;
				}
				}
			}

			// draw the final interaction
			RB_DrawSingleInteraction(&inter);

			renderLog.CloseBlock();
		}
	}

	if (useLightDepthBounds && lightDepthBoundsDisabled) {
		GL_DepthBoundsTest(vLight->scissorRect.zmin, vLight->scissorRect.zmax);
	}

	renderProgManager.Unbind();
}

/*
==============================================================================================

DRAW GBUFFER

==============================================================================================
*/
/*
==================
RB_DrawGBuffer
==================
*/
static const DX12Rendering::Commands::FenceValue RB_DrawGBuffer(drawSurf_t** drawSurfs, int numDrawSurfs) {
	constexpr UINT surfaceCount = 5;
	const DX12Rendering::eRenderSurface surfaces[surfaceCount] = {
		DX12Rendering::eRenderSurface::FlatNormal,
		DX12Rendering::eRenderSurface::Position,
		DX12Rendering::eRenderSurface::Normal,
		DX12Rendering::eRenderSurface::Albedo,
		DX12Rendering::eRenderSurface::SpecularColor
	};

	DX12Rendering::RenderPassBlock renderPassBlock("RB_DrawGBuffer", DX12Rendering::Commands::DIRECT, surfaces, surfaceCount);

	//First fill in the sub view surfaces
	int surfNum;
	for (surfNum = 0; surfNum < numDrawSurfs; surfNum++)
	{
		if (drawSurfs[surfNum]->material->GetSort() != SS_SUBVIEW) {
			break;
		}
		// TODO: Handle Sub Views
	}

//TODO: Clear the buffers
//TODO: Implement perforated serfaces
//TODO: Move to it's own buffer to render and cast rays'


	GBufferSurfaceConstants surfaceConstants = {};
	// continue checking past the subview surfaces
	for (; surfNum < numDrawSurfs; surfNum++) {
		const drawSurf_t* surf = drawSurfs[surfNum];
		const idMaterial* shader = surf->material;

		if (!shader->ReceivesLighting())
		{
			// The GBuffer is only used to calculate light interactions. No point in adding these objects
			continue;
		}

		// translucent surfaces should not write to the depth or normal maps
		if (shader->Coverage() == MC_TRANSLUCENT)
		{
			continue;

			// Removing all transparent surfaces as they will be handled in the forward renderer.

			//if (shader->GetSort() != SS_DECAL)
			//{
			//	// Only decals can be added onto objects.
			//	continue;
			//}

			////Materials are additive blended to their background.
			//GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | GLS_DEPTHFUNC_LESS | GLS_STENCIL_FUNC_ALWAYS);

			//if (surf->jointCache) {
			//	renderProgManager.BindShader_GBufferTransparentSkinned();
			//}
			//else {
			//	renderProgManager.BindShader_GBufferTransparent();
			//}
		}
		else
		{
			// draw all the opaque surfaces and build up a list of perforated surfaces that
			// we will defer drawing until all opaque surfaces are done
			GL_State(GLS_DEPTHMASK | GLS_DEPTHFUNC_EQUAL | GLS_STENCIL_FUNC_ALWAYS);

			if (surf->jointCache) {
				renderProgManager.BindShader_GBufferSkinned();
			}
			else {
				renderProgManager.BindShader_GBuffer();
			}
		}


		// set mvp matrix
		if (surf->space != backEnd.currentSpace) {
			RB_SetMVP(surf->space->mvp);
			backEnd.currentSpace = surf->space;

			// set the Normal Matrix (technically it's the transpose of the model matrix)
			const float* modelMatrix = surf->space->modelMatrix;
			idMat4 normalMatrix(
				modelMatrix[0], modelMatrix[1], modelMatrix[2], modelMatrix[3],
				modelMatrix[4], modelMatrix[5], modelMatrix[6], modelMatrix[7],
				modelMatrix[8], modelMatrix[9], modelMatrix[10], modelMatrix[11],
				modelMatrix[12], modelMatrix[13], modelMatrix[14], modelMatrix[15]
			); // TODO: Precalculate value
			//normalMatrix = normalMatrix.Transpose();
			normalMatrix = normalMatrix.Inverse();
			float normalMatrixTranspose[16];
			R_MatrixTranspose(normalMatrix.ToFloatPtr(), normalMatrixTranspose);
			//TODO: Find the appropriate matrix
			SetVertexParms(RENDERPARM_NORMALMATRIX_X, normalMatrix.ToFloatPtr(), 4);

			// set model Matrix
			float modelMatrixTranspose[16];
			R_MatrixTranspose(surf->space->modelMatrix, modelMatrixTranspose);
			SetVertexParms(RENDERPARM_MODELMATRIX_X, modelMatrixTranspose, 4);

			// Set ModelView Matrix
			float modelViewMatrixTranspose[16];
			R_MatrixTranspose(surf->space->modelViewMatrix, modelViewMatrixTranspose);
			SetVertexParms(RENDERPARM_MODELVIEWMATRIX_X, modelViewMatrixTranspose, 4);
		}

		const idMaterial* surfaceShader = surf->material;
		if (surfaceShader->GetFastPathBumpImage() && !r_skipInteractionFastPath.GetBool()) {
			// texture 0 will be the per-surface bump map
			surfaceConstants.bumpMapIndex = LoadBindlessTexture(surfaceShader->GetFastPathBumpImage());

			// texture 3 is the per-surface diffuse map
			surfaceConstants.albedoIndex = LoadBindlessTexture(surfaceShader->GetFastPathDiffuseImage());

			// texture 4 is the per-surface specular map
			surfaceConstants.specularIndex = LoadBindlessTexture(surfaceShader->GetFastPathSpecularImage());

			const idVec4 sMatrix(1, 0, 0, 0);
			const idVec4 tMatrix(0, 1, 0, 0);

			SetVertexParm(RENDERPARM_BUMPMATRIX_S, sMatrix.ToFloatPtr());
			SetVertexParm(RENDERPARM_BUMPMATRIX_T, tMatrix.ToFloatPtr());

			SetVertexParm(RENDERPARM_DIFFUSEMATRIX_S, sMatrix.ToFloatPtr());
			SetVertexParm(RENDERPARM_DIFFUSEMATRIX_T, tMatrix.ToFloatPtr());

			SetVertexParm(RENDERPARM_SPECULARMATRIX_S, sMatrix.ToFloatPtr());
			SetVertexParm(RENDERPARM_SPECULARMATRIX_T, tMatrix.ToFloatPtr());
		}
		else
		{
			const float* surfaceRegs = surf->shaderRegisters;
			bool bumpMapFound = false;
			bool diffuseMapFound = false;
			bool specularMapFound = false;

			for (int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++) {
				const shaderStage_t* surfaceStage = surfaceShader->GetStage(surfaceStageNum);

				switch (surfaceStage->lighting) {
				case SL_COVERAGE: {
					// ignore any coverage stages since they should only be used for the depth fill pass
					// for diffuse stages that use alpha test.
					break;
				}
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}

					if (bumpMapFound)
					{
						// Already loaded the texture, draw the surface.
						RB_DrawElementsWithCounters(surf);
					}
					bumpMapFound = true;

					// texture 0 will be the per-surface bump map
					surfaceConstants.bumpMapIndex = LoadBindlessTexture(surfaceStage->texture.image);

					idVec4 bumpMatrix[2];
					RB_SetupInteractionStage(surfaceStage, surfaceRegs, NULL,
						bumpMatrix, NULL);

					// bump matrix
					SetVertexParm(RENDERPARM_BUMPMATRIX_S, bumpMatrix[0].ToFloatPtr());
					SetVertexParm(RENDERPARM_BUMPMATRIX_T, bumpMatrix[1].ToFloatPtr());

					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}

					if (diffuseMapFound)
					{
						// Already loaded the texture, draw the surface.
						RB_DrawElementsWithCounters(surf);
					}
					diffuseMapFound = true;

					// texture 3 will be the diffuse map
					surfaceConstants.albedoIndex = LoadBindlessTexture(surfaceStage->texture.image);

					idVec4 matrix[2];
					RB_SetupInteractionStage(surfaceStage, surfaceRegs, NULL,
						matrix, NULL);

					// bump matrix
					SetVertexParm(RENDERPARM_DIFFUSEMATRIX_S, matrix[0].ToFloatPtr());
					SetVertexParm(RENDERPARM_DIFFUSEMATRIX_T, matrix[1].ToFloatPtr());

					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}

					if (specularMapFound)
					{
						// Already loaded the texture, draw the surface.
						RB_DrawElementsWithCounters(surf);
					}
					specularMapFound = true;

					// texture 3 will be the diffuse map
					surfaceConstants.specularIndex = LoadBindlessTexture(surfaceStage->texture.image);

					idVec4 matrix[2];
					RB_SetupInteractionStage(surfaceStage, surfaceRegs, NULL,
						matrix, NULL);

					// bump matrix
					SetVertexParm(RENDERPARM_SPECULARMATRIX_S, matrix[0].ToFloatPtr());
					SetVertexParm(RENDERPARM_SPECULARMATRIX_T, matrix[1].ToFloatPtr());

					break;
				}
				}
			}
		}

		// draw it solid
		RB_DrawElementsWithCounters(surf, &surfaceConstants, sizeof(GBufferSurfaceConstants));
	}

	DX12Rendering::Commands::CommandManager* commandManager = renderPassBlock.GetCommandManager();
	const DX12Rendering::Commands::FenceValue fence = commandManager->InsertFenceSignal();

	return fence;
}

/*
==============================================================================================

DRAW INTERACTIONS

==============================================================================================
*/
/*
==================
RB_DrawInteractions
==================
*/
static void RB_DrawInteractions(const bool useRaytracing) {
	if (r_skipInteractions.GetBool()) {
		return;
	}

	renderLog.OpenMainBlock(MRB_DRAW_INTERACTIONS);
	renderLog.OpenBlock("RB_DrawInteractions");

	DX12Rendering::RenderPassBlock renderPassBlock("DrawInteractions", DX12Rendering::Commands::DIRECT);
	
	DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();

	GL_SelectTexture(0);

	const bool useLightDepthBounds = r_useLightDepthBounds.GetBool();

	//
	// for each light, perform shadowing and adding
	//
	for (const viewLight_t* vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next) {
		// do fogging later
		if (vLight->lightShader->IsFogLight()) {
			continue;
		}
		if (vLight->lightShader->IsBlendLight()) {
			continue;
		}

		if (vLight->localInteractions == NULL && vLight->globalInteractions == NULL && vLight->translucentInteractions == NULL) {
			continue;
		}

		// Set command list.
		//dxRenderer.ExecuteCommandList();
		//dxRenderer.ResetCommandList();

		const idMaterial* lightShader = vLight->lightShader;
		renderLog.OpenBlock(lightShader->GetName());

		// set the depth bounds for the whole light
		if (useLightDepthBounds) {
			GL_DepthBoundsTest(vLight->scissorRect.zmin, vLight->scissorRect.zmax);
		}

		// only need to clear the stencil buffer and perform stencil testing if there are shadows
		const bool performStencilTest = (vLight->globalShadows != NULL || vLight->localShadows != NULL);

		// mirror flips the sense of the stencil select, and I don't want to risk accidentally breaking it
		// in the normal case, so simply disable the stencil select in the mirror case
		const bool useLightStencilSelect = ( r_useLightStencilSelect.GetBool() && backEnd.viewDef->isMirror == false);

		if (performStencilTest) {
			if (useLightStencilSelect) {
				// write a stencil mask for the visible light bounds to hi-stencil
				RB_StencilSelectLight(vLight);
			}
			else {
				// always clear whole S-Cull tiles
				idScreenRect rect;
				rect.x1 = (vLight->scissorRect.x1 + 0) & ~15;
				rect.y1 = (vLight->scissorRect.y1 + 0) & ~15;
				rect.x2 = (vLight->scissorRect.x2 + 15) & ~15;
				rect.y2 = (vLight->scissorRect.y2 + 15) & ~15;

				if (!backEnd.currentScissor.Equals(rect) && r_useScissor.GetBool()) {
					GL_Scissor(backEnd.viewDef->viewport.x1 + rect.x1,
						backEnd.viewDef->viewport.y1 + rect.y1,
						rect.x2 + 1 - rect.x1,
						rect.y2 + 1 - rect.y1);
					backEnd.currentScissor = rect;
				}
				GL_State(GLS_DEFAULT);	// make sure stencil mask passes for the clear
				GL_Clear(false, false, true, STENCIL_SHADOW_TEST_VALUE, 0.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		if (!useRaytracing)
		{
			if (vLight->globalShadows != NULL) {
				DX12Rendering::Commands::CommandManagerCycleBlock childBlock(renderPassBlock.GetCommandManager(), "Global Light Shadows");

				renderLog.OpenBlock("Global Light Shadows");
				RB_StencilShadowPass(vLight->globalShadows, vLight);
				renderLog.CloseBlock();
			}

			if (vLight->localInteractions != NULL) {
				DX12Rendering::Commands::CommandManagerCycleBlock childBlock(renderPassBlock.GetCommandManager(), "Local Light Interactions");

				renderLog.OpenBlock("Local Light Interactions");
				RB_RenderInteractions(vLight->localInteractions, vLight, GLS_DEPTHFUNC_EQUAL, performStencilTest, useLightDepthBounds);
				renderLog.CloseBlock();
			}

			if (vLight->localShadows != NULL) {
				DX12Rendering::Commands::CommandManagerCycleBlock childBlock(renderPassBlock.GetCommandManager(), "Local Light Shadow");

				renderLog.OpenBlock("Local Light Shadows");
				RB_StencilShadowPass(vLight->localShadows, vLight);
				renderLog.CloseBlock();
			}

			if (vLight->globalInteractions != NULL) {
				DX12Rendering::Commands::CommandManagerCycleBlock childBlock(renderPassBlock.GetCommandManager(), "Global Light Interactions");

				renderLog.OpenBlock("Global Light Interactions");
				RB_RenderInteractions(vLight->globalInteractions, vLight, GLS_DEPTHFUNC_EQUAL, performStencilTest, useLightDepthBounds);
				renderLog.CloseBlock();
			}
		}


		if (vLight->translucentInteractions != NULL && !r_skipTranslucent.GetBool()) {
			//DX12Rendering::Commands::CommandManagerCycleBlock childBlock(renderPassBlock.GetCommandManager(), "Global Light Interactions");

			renderLog.OpenBlock("Translucent Interactions");

			// Disable the depth bounds test because translucent surfaces don't work with
			// the depth bounds tests since they did not write depth during the depth pass.
			if (useLightDepthBounds) {
				GL_DepthBoundsTest(0.0f, 0.0f);
			}

			// The depth buffer wasn't filled in for translucent surfaces, so they
			// can never be constrained to perforated surfaces with the depthfunc equal.

			// Translucent surfaces do not receive shadows. This is a case where a
			// shadow buffer solution would work but stencil shadows do not because
			// stencil shadows only affect surfaces that contribute to the view depth
			// buffer and translucent surfaces do not contribute to the view depth buffer.

			RB_RenderInteractions(vLight->translucentInteractions, vLight, GLS_DEPTHFUNC_LESS, false, false);

			renderLog.CloseBlock();
		}

		renderLog.CloseBlock();
	}

	// disable stencil shadow test
	GL_State(GLS_DEFAULT);

	// unbind texture units
	for (int i = 0; i < TEXTURE_REGISTER_COUNT; i++) {
		GL_SelectTexture(i);
		globalImages->BindNull();
	}
	GL_SelectTexture(0);

	// reset depth bounds
	if (useLightDepthBounds) {
		GL_DepthBoundsTest(0.0f, 0.0f);
	}

	// Reset the shadowmap for the next frame
	{
		auto commandList = renderPassBlock.GetCommandManager()->RequestNewCommandList();

		auto screenShadows = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SHADOWMAP);
		textureManager->SetTextureState(screenShadows, D3D12_RESOURCE_STATE_COMMON, commandList);

		commandList->Close();
	}

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

/*
=====================
RB_DrawShaderPasses

Draw non-light dependent passes

If we are rendering Guis, the drawSurf_t::sort value is a depth offset that can
be multiplied by guiEye for polarity and screenSeparation for scale.
=====================
*/
static int RB_DrawShaderPasses(const drawSurf_t* const* const drawSurfs, const int numDrawSurfs,
	const float guiStereoScreenOffset, const int stereoEye) {
	// only obey skipAmbient if we are rendering a view
	if (backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool()) {
		return numDrawSurfs;
	}

	renderLog.OpenBlock("RB_DrawShaderPasses");

	GL_SelectTexture(1);
	globalImages->BindNull();

	GL_SelectTexture(0);

	backEnd.currentSpace = (const viewEntity_t*)1;	// using NULL makes /analyze think surf->space needs to be checked...
	float currentGuiStereoOffset = 0.0f;

	int i = 0;
	for (; i < numDrawSurfs; i++) {
		const drawSurf_t* surf = drawSurfs[i];
		const idMaterial* shader = surf->material;

		if (!shader->HasAmbient()) {
			continue;
		}

		if (shader->IsPortalSky()) {
			continue;
		}

		// some deforms may disable themselves by setting numIndexes = 0
		if (surf->numIndexes == 0) {
			continue;
		}

		if (shader->SuppressInSubview()) {
			continue;
		}

		if (backEnd.viewDef->isXraySubview && surf->space->entityDef) {
			if (surf->space->entityDef->parms.xrayIndex != 2) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if (shader->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied) {
			break;
		}

		renderLog.OpenBlock(shader->GetName());

		// determine the stereoDepth offset 
		// guiStereoScreenOffset will always be zero for 3D views, so the !=
		// check will never force an update due to the current sort value.
		const float thisGuiStereoOffset = guiStereoScreenOffset * surf->sort;

		// change the matrix and other space related vars if needed
		if (surf->space != backEnd.currentSpace || thisGuiStereoOffset != currentGuiStereoOffset) {
			backEnd.currentSpace = surf->space;
			currentGuiStereoOffset = thisGuiStereoOffset;

			const viewEntity_t* space = backEnd.currentSpace;

			RB_SetMVP(space->mvp);

			// set eye position in local space
			idVec4 localViewOrigin(1.0f);
			R_GlobalPointToLocal(space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3());
			SetVertexParm(RENDERPARM_LOCALVIEWORIGIN, localViewOrigin.ToFloatPtr());

			// set model Matrix
			float modelMatrixTranspose[16];
			R_MatrixTranspose(space->modelMatrix, modelMatrixTranspose);
			SetVertexParms(RENDERPARM_MODELMATRIX_X, modelMatrixTranspose, 4);

			// Set ModelView Matrix
			float modelViewMatrixTranspose[16];
			R_MatrixTranspose(space->modelViewMatrix, modelViewMatrixTranspose);
			SetVertexParms(RENDERPARM_MODELVIEWMATRIX_X, modelViewMatrixTranspose, 4);
		}

		// change the scissor if needed
		if (!backEnd.currentScissor.Equals(surf->scissorRect) && r_useScissor.GetBool()) {
			GL_Scissor(backEnd.viewDef->viewport.x1 + surf->scissorRect.x1,
				backEnd.viewDef->viewport.y1 + surf->scissorRect.y1,
				surf->scissorRect.x2 + 1 - surf->scissorRect.x1,
				surf->scissorRect.y2 + 1 - surf->scissorRect.y1);
			backEnd.currentScissor = surf->scissorRect;
		}

		// get the expressions for conditionals / color / texcoords
		const float* regs = surf->shaderRegisters;

		// set face culling appropriately
		if (surf->space->isGuiSurface) {
			GL_Cull(CT_TWO_SIDED);
		}
		else {
			GL_Cull(shader->GetCullType());
		}

		uint64 surfGLState = surf->extraGLState;

		// set polygon offset if necessary
		if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
			GL_PolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
			surfGLState = GLS_POLYGON_OFFSET;
		}

		for (int stage = 0; stage < shader->GetNumStages(); stage++) {
			const shaderStage_t* pStage = shader->GetStage(stage);

			// check the enable condition
			if (regs[pStage->conditionRegister] == 0) {
				continue;
			}

			// skip the stages involved in lighting
			if (pStage->lighting != SL_AMBIENT) {
				continue;
			}

			uint64 stageGLState = surfGLState;
			if ((surfGLState & GLS_OVERRIDE) == 0) {
				stageGLState |= pStage->drawStateBits;
			}

			// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
			if ((stageGLState & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE)) {
				continue;
			}

			// see if we are a new-style stage
			newShaderStage_t* newStage = pStage->newStage;
			if (newStage != NULL) {
				//--------------------------
				//
				// new style stages
				//
				//--------------------------
				if (r_skipNewAmbient.GetBool()) {
					continue;
				}
				renderLog.OpenBlock("New Shader Stage");

				const UINT gpuIndex = dxRenderer.StartSurfaceSettings();
				GL_State(stageGLState);

				renderProgManager.BindShader(newStage->glslProgram, newStage->glslProgram);

				for (int j = 0; j < newStage->numVertexParms; j++) {
					float parm[4];
					parm[0] = regs[newStage->vertexParms[j][0]];
					parm[1] = regs[newStage->vertexParms[j][1]];
					parm[2] = regs[newStage->vertexParms[j][2]];
					parm[3] = regs[newStage->vertexParms[j][3]];
					SetVertexParm((renderParm_t)(RENDERPARM_USER + j), parm);
				}

				// set rpEnableSkinning if the shader has optional support for skinning
				if (surf->jointCache && renderProgManager.ShaderHasOptionalSkinning()) {
					const idVec4 skinningParm(1.0f);
					SetVertexParm(RENDERPARM_ENABLE_SKINNING, skinningParm.ToFloatPtr());
				}

				// bind texture units
				for (int j = 0; j < newStage->numFragmentProgramImages; j++) {
					idImage* image = newStage->fragmentProgramImages[j];
					if (image != NULL) {
						GL_SelectTexture(j);
						image->Bind();
					}
				}

				// draw it
				RB_DrawElementsWithCounters(surf);

				// unbind texture units
				for (int j = 0; j < newStage->numFragmentProgramImages; j++) {
					idImage* image = newStage->fragmentProgramImages[j];
					if (image != NULL) {
						GL_SelectTexture(j);
						globalImages->BindNull();
					}
				}

				// clear rpEnableSkinning if it was set
				if (surf->jointCache && renderProgManager.ShaderHasOptionalSkinning()) {
					const idVec4 skinningParm(0.0f);
					SetVertexParm(RENDERPARM_ENABLE_SKINNING, skinningParm.ToFloatPtr());
				}

				GL_SelectTexture(0);
				renderProgManager.Unbind();

				renderLog.CloseBlock();
				continue;
			}

			//--------------------------
			//
			// old style stages
			//
			//--------------------------

			// set the color
			float color[4];
			color[0] = regs[pStage->color.registers[0]];
			color[1] = regs[pStage->color.registers[1]];
			color[2] = regs[pStage->color.registers[2]];
			color[3] = regs[pStage->color.registers[3]];

			// skip the entire stage if an add would be black
			if ((stageGLState & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
				&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
				continue;
			}

			// skip the entire stage if a blend would be completely transparent
			if ((stageGLState & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
				&& color[3] <= 0) {
				continue;
			}

			stageVertexColor_t svc = pStage->vertexColor;

			renderLog.OpenBlock("Old Shader Stage");
			GL_Color(color);

			if (surf->space->isGuiSurface) {
				// Force gui surfaces to always be SVC_MODULATE
				svc = SVC_MODULATE;

				// use special shaders for bink cinematics
				if (pStage->texture.cinematic) {
					if ((stageGLState & GLS_OVERRIDE) != 0) {
						// This is a hack... Only SWF Guis set GLS_OVERRIDE
						// Old style guis do not, and we don't want them to use the new GUI renederProg
						renderProgManager.BindShader_BinkGUI();
					}
					else {
						renderProgManager.BindShader_Bink();
					}
				}
				else {
					if ((stageGLState & GLS_OVERRIDE) != 0) {
						// This is a hack... Only SWF Guis set GLS_OVERRIDE
						// Old style guis do not, and we don't want them to use the new GUI renderProg
						renderProgManager.BindShader_GUI();
					}
					else {
						if (surf->jointCache) {
							renderProgManager.BindShader_TextureVertexColorSkinned();
						}
						else {
							renderProgManager.BindShader_TextureVertexColor();
						}
					}
				}
			}
			else if ((pStage->texture.texgen == TG_SCREEN) || (pStage->texture.texgen == TG_SCREEN2)) {
				renderProgManager.BindShader_TextureTexGenVertexColor();
			}
			else if (pStage->texture.cinematic) {
				renderProgManager.BindShader_Bink();
			}
			else {
				if (surf->jointCache) {
					renderProgManager.BindShader_TextureVertexColorSkinned();
				}
				else {
					renderProgManager.BindShader_TextureVertexColor();
				}
			}

			RB_SetVertexColorParms(svc);

			// bind the texture
			RB_BindVariableStageImage(&pStage->texture, regs);

			// set privatePolygonOffset if necessary
			if (pStage->privatePolygonOffset) {
				GL_PolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
				stageGLState |= GLS_POLYGON_OFFSET;
			}

			// set the state
			GL_State(stageGLState);

			RB_PrepareStageTexturing(pStage, surf);

			// draw it
			RB_DrawElementsWithCounters(surf);

			// unset privatePolygonOffset if necessary
			if (pStage->privatePolygonOffset) {
				GL_PolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
			}
			renderLog.CloseBlock();
		}

		renderLog.CloseBlock();
	}

	GL_Cull(CT_FRONT_SIDED);
	GL_Color(1.0f, 1.0f, 1.0f);

	renderLog.CloseBlock();
	return i;
}

/*
=====================
RB_T_BasicFog
=====================
*/
static void RB_T_BasicFog(const drawSurf_t* drawSurfs, const idPlane fogPlanes[4], const idRenderMatrix* inverseBaseLightProject) {
	backEnd.currentSpace = NULL;

	for (const drawSurf_t* drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight) {
		if (drawSurf->scissorRect.IsEmpty()) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		if (!backEnd.currentScissor.Equals(drawSurf->scissorRect) && r_useScissor.GetBool()) {
			// change the scissor
			GL_Scissor(backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
				backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
				drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
				drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1);
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if (drawSurf->space != backEnd.currentSpace) {
			idPlane localFogPlanes[4];
			if (inverseBaseLightProject == NULL) {
				RB_SetMVP(drawSurf->space->mvp);
				for (int i = 0; i < 4; i++) {
					R_GlobalPlaneToLocal(drawSurf->space->modelMatrix, fogPlanes[i], localFogPlanes[i]);
				}
			}
			else {
				idRenderMatrix invProjectMVPMatrix;
				idRenderMatrix::Multiply(backEnd.viewDef->worldSpace.mvp, *inverseBaseLightProject, invProjectMVPMatrix);
				RB_SetMVP(invProjectMVPMatrix);
				for (int i = 0; i < 4; i++) {
					inverseBaseLightProject->InverseTransformPlane(fogPlanes[i], localFogPlanes[i], false);
				}
			}

			SetVertexParm(RENDERPARM_TEXGEN_0_S, localFogPlanes[0].ToFloatPtr());
			SetVertexParm(RENDERPARM_TEXGEN_0_T, localFogPlanes[1].ToFloatPtr());
			SetVertexParm(RENDERPARM_TEXGEN_1_T, localFogPlanes[2].ToFloatPtr());
			SetVertexParm(RENDERPARM_TEXGEN_1_S, localFogPlanes[3].ToFloatPtr());

			backEnd.currentSpace = (inverseBaseLightProject == NULL) ? drawSurf->space : NULL;
		}

		if (drawSurf->jointCache) {
			renderProgManager.BindShader_FogSkinned();
		}
		else {
			renderProgManager.BindShader_Fog();
		}

		RB_DrawElementsWithCounters(drawSurf);
	}
}

/*
==================
RB_FogPass
==================
*/
static void RB_FogPass(const drawSurf_t* drawSurfs, const drawSurf_t* drawSurfs2, const viewLight_t* vLight) {
	renderLog.OpenBlock(vLight->lightShader->GetName());

	// find the current color and density of the fog
	const idMaterial* lightShader = vLight->lightShader;
	const float* regs = vLight->shaderRegisters;
	// assume fog shaders have only a single stage
	const shaderStage_t* stage = lightShader->GetStage(0);

	float lightColor[4];
	lightColor[0] = regs[stage->color.registers[0]];
	lightColor[1] = regs[stage->color.registers[1]];
	lightColor[2] = regs[stage->color.registers[2]];
	lightColor[3] = regs[stage->color.registers[3]];

	GL_Color(lightColor);

	// calculate the falloff planes
	float a;

	// if they left the default value on, set a fog distance of 500
	if (lightColor[3] <= 1.0f) {
		a = -0.5f / DEFAULT_FOG_DISTANCE;
	}
	else {
		// otherwise, distance = alpha color
		a = -0.5f / lightColor[3];
	}

	// texture 0 is the falloff image
	GL_SelectTexture(0);
	globalImages->fogImage->Bind();

	// texture 1 is the entering plane fade correction
	GL_SelectTexture(1);
	globalImages->fogEnterImage->Bind();

	// S is based on the view origin
	const float s = vLight->fogPlane.Distance(backEnd.viewDef->renderView.vieworg);

	const float FOG_SCALE = 0.001f;

	idPlane fogPlanes[4];

	// S-0
	fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0 * 4 + 2];
	fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[1 * 4 + 2];
	fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2 * 4 + 2];
	fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[3 * 4 + 2] + 0.5f;

	// T-0
	fogPlanes[1][0] = 0.0f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[0*4+0];
	fogPlanes[1][1] = 0.0f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[1*4+0];
	fogPlanes[1][2] = 0.0f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[2*4+0];
	fogPlanes[1][3] = 0.5f;//a * backEnd.viewDef->worldSpace.modelViewMatrix[3*4+0] + 0.5f;

	// T-1 will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogPlanes[2][0] = FOG_SCALE * vLight->fogPlane[0];
	fogPlanes[2][1] = FOG_SCALE * vLight->fogPlane[1];
	fogPlanes[2][2] = FOG_SCALE * vLight->fogPlane[2];
	fogPlanes[2][3] = FOG_SCALE * vLight->fogPlane[3] + FOG_ENTER;

	// S-1
	fogPlanes[3][0] = 0.0f;
	fogPlanes[3][1] = 0.0f;
	fogPlanes[3][2] = 0.0f;
	fogPlanes[3][3] = FOG_SCALE * s + FOG_ENTER;

	// draw it
	GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL);
	RB_T_BasicFog(drawSurfs, fogPlanes, NULL);
	RB_T_BasicFog(drawSurfs2, fogPlanes, NULL);

	// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
	// of depthfunc_equal
	GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS);
	GL_Cull(CT_BACK_SIDED);

	backEnd.zeroOneCubeSurface.space = &backEnd.viewDef->worldSpace;
	backEnd.zeroOneCubeSurface.scissorRect = backEnd.viewDef->scissor;
	RB_T_BasicFog(&backEnd.zeroOneCubeSurface, fogPlanes, &vLight->inverseBaseLightProject);

	GL_Cull(CT_FRONT_SIDED);

	GL_SelectTexture(1);
	globalImages->BindNull();

	GL_SelectTexture(0);

	renderProgManager.Unbind();

	renderLog.CloseBlock();
}

/*
=====================
RB_T_BlendLight
=====================
*/
static void RB_T_BlendLight(const drawSurf_t* drawSurfs, const viewLight_t* vLight) {
	backEnd.currentSpace = NULL;

	for (const drawSurf_t* drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight) {
		if (drawSurf->scissorRect.IsEmpty()) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		if (!backEnd.currentScissor.Equals(drawSurf->scissorRect) && r_useScissor.GetBool()) {
			// change the scissor
			GL_Scissor(backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
				backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
				drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
				drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1);
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if (drawSurf->space != backEnd.currentSpace) {
			// change the matrix
			RB_SetMVP(drawSurf->space->mvp);

			// change the light projection matrix
			idPlane	lightProjectInCurrentSpace[4];
			for (int i = 0; i < 4; i++) {
				R_GlobalPlaneToLocal(drawSurf->space->modelMatrix, vLight->lightProject[i], lightProjectInCurrentSpace[i]);
			}

			SetVertexParm(RENDERPARM_TEXGEN_0_S, lightProjectInCurrentSpace[0].ToFloatPtr());
			SetVertexParm(RENDERPARM_TEXGEN_0_T, lightProjectInCurrentSpace[1].ToFloatPtr());
			SetVertexParm(RENDERPARM_TEXGEN_0_Q, lightProjectInCurrentSpace[2].ToFloatPtr());
			SetVertexParm(RENDERPARM_TEXGEN_1_S, lightProjectInCurrentSpace[3].ToFloatPtr());	// falloff

			backEnd.currentSpace = drawSurf->space;
		}

		RB_DrawElementsWithCounters(drawSurf);
	}
}

/*
=====================
RB_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
static void RB_BlendLight(const drawSurf_t* drawSurfs, const drawSurf_t* drawSurfs2, const viewLight_t* vLight) {
	if (drawSurfs == NULL) {
		return;
	}
	if (r_skipBlendLights.GetBool()) {
		return;
	}
	renderLog.OpenBlock(vLight->lightShader->GetName());

	const idMaterial* lightShader = vLight->lightShader;
	const float* regs = vLight->shaderRegisters;

	// texture 1 will get the falloff texture
	GL_SelectTexture(1);
	vLight->falloffImage->Bind();

	// texture 0 will get the projected texture
	GL_SelectTexture(0);

	renderProgManager.BindShader_BlendLight();

	for (int i = 0; i < lightShader->GetNumStages(); i++) {
		const shaderStage_t* stage = lightShader->GetStage(i);

		if (!regs[stage->conditionRegister]) {
			continue;
		}

		GL_State(GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL);

		GL_SelectTexture(0);
		stage->texture.image->Bind();

		if (stage->texture.hasMatrix) {
			RB_LoadShaderTextureMatrix(regs, &stage->texture);
		}

		// get the modulate values from the light, including alpha, unlike normal lights
		float lightColor[4];
		lightColor[0] = regs[stage->color.registers[0]];
		lightColor[1] = regs[stage->color.registers[1]];
		lightColor[2] = regs[stage->color.registers[2]];
		lightColor[3] = regs[stage->color.registers[3]];
		GL_Color(lightColor);

		RB_T_BlendLight(drawSurfs, vLight);
		RB_T_BlendLight(drawSurfs2, vLight);
	}

	GL_SelectTexture(1);
	globalImages->BindNull();

	GL_SelectTexture(0);

	renderProgManager.Unbind();
	renderLog.CloseBlock();
}

/*
==================
RB_FogAllLights
==================
*/
static void RB_FogAllLights() {
	if (r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0
		|| backEnd.viewDef->isXraySubview /* don't fog in xray mode*/) {
		return;
	}
	renderLog.OpenMainBlock(MRB_FOG_ALL_LIGHTS);
	renderLog.OpenBlock("RB_FogAllLights");

	DX12Rendering::RenderPassBlock renderPass("RB_FogAllLights", DX12Rendering::Commands::DIRECT);

	// force fog plane to recalculate
	backEnd.currentSpace = NULL;

	for (viewLight_t* vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next) {
		if (vLight->lightShader->IsFogLight()) {
			RB_FogPass(vLight->globalInteractions, vLight->localInteractions, vLight);
		}
		else if (vLight->lightShader->IsBlendLight()) {
			RB_BlendLight(vLight->globalInteractions, vLight->localInteractions, vLight);
		}
	}

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

void RB_DrawCombinedGBufferResults()
{
	DX12Rendering::RenderPassBlock renderPassBlock("DXR_GenerateResult", DX12Rendering::Commands::DIRECT);
	
	DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();

	
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS);
	GL_Cull(CT_TWO_SIDED);

	int screenWidth = renderSystem->GetWidth();
	int screenHeight = renderSystem->GetHeight();

	// set the window clipping
	GL_Viewport(0, 0, screenWidth, screenHeight);
	GL_Scissor(0, 0, screenWidth, screenHeight);
	renderProgManager.BindShader_GBufferCombinedResult();

	// Prepare the textures
	{
		auto commandList = renderPassBlock.GetCommandManager()->RequestNewCommandList();

		// Wait for any copy operations to finish
		auto diffuse = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_DIFFUSE);
		auto specular = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SPECULAR);

		// Prepare the surfaces for rendering
		commandList->AddPreFenceWait(diffuse->GetLastFenceValue());
		GL_SelectTexture(0);
		dxRenderer.SetTexture(diffuse);

		commandList->AddPreFenceWait(specular->GetLastFenceValue());
		GL_SelectTexture(1);
		dxRenderer.SetTexture(specular);

		commandList->Close();
	}

	// Draw
	RB_DrawElementsWithCounters(&backEnd.unitSquareSurface);

	{
		auto commandList = renderPassBlock.GetCommandManager()->RequestNewCommandList();

		// Once completed reset the states on the diffuse and specular textures
		textureManager->SetTextureState(textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_DIFFUSE), D3D12_RESOURCE_STATE_COMMON, commandList);
		textureManager->SetTextureState(textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SPECULAR), D3D12_RESOURCE_STATE_COMMON, commandList);

		commandList->Close();
	}
}

void RB_CalculateDynamicObjects(const viewDef_t* viewDef, const bool raytracedEnabled)
{
	const int numDrawSurfs = viewDef->numDrawSurfs;

	// Update all surfaces and bones as needed
	{
		DX12Rendering::Commands::CommandManager* commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE);
		DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RB_DrawViewInternal::ComputeBones");

		dxRenderer.StartComputeSurfaceBones();
		commandManager->Execute();

		// Entity definitions are gouped together. This way we can put them into a single blas
		const viewEntity_t* lastEntity = nullptr;
		dxHandle_t blasHandle = 1;
		std::vector<DX12Rendering::RaytracingGeometryArgument> rtGeometry;
		rtGeometry.reserve(16);

		UINT outVertexBufferOffset = 0;

		for (int i = 0; i < numDrawSurfs; i++) {
			const drawSurf_t* ds = viewDef->drawSurfs[i];

			if (ds->space != lastEntity)
			{
				if (rtGeometry.size() > 0)
				{
					// Add the BLAS and TLAS
					dxRenderer.DXR_UpdateBLAS(blasHandle, std::string("DynamicBLAS: %d", blasHandle).c_str(), false, rtGeometry.size(), rtGeometry.data());

					UINT instanceMask = DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_NONE;
					{
						// Calculate the instance mask
						if (true || lastEntity->staticShadowVolumes != nullptr || lastEntity->dynamicShadowVolumes != nullptr) // TODO: find a better way to mark these as shadow casting.
						{
							instanceMask |= DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_CAST_SHADOW;
						}
					}

					dxRenderer.DXR_AddBLASToTLAS(
						lastEntity->entityIndex,
						blasHandle,
						lastEntity->modelRenderMatrix[0], // TODO: Find thre right one
						DX12Rendering::ACCELERATION_INSTANCE_TYPE::INSTANCE_TYPE_DYNAMIC,
						static_cast<DX12Rendering::ACCELLERATION_INSTANCE_MASK>(instanceMask)
					);

					++blasHandle;
					rtGeometry.clear();
				}

				lastEntity = ds->space;
			}

			if (ds->jointCache)
			{

				// get vertex buffer
				idVertexBuffer* const vertexBuffer = GetVertexBuffer(ds->ambientCache);
				const int vertOffset = GetVertexBufferOffset(ds->ambientCache);
				const UINT vertSize = GetVertexBufferSize(ds->ambientCache);
				const UINT vertIndexCount = vertSize / sizeof(idDrawVert);

				if (vertexBuffer == nullptr)
				{
					// An error occured when grabbing the vertex buffer data.
					return;
				}

				DX12Rendering::Geometry::VertexBuffer* const apiVertexBuffer = static_cast<DX12Rendering::Geometry::VertexBuffer*>(vertexBuffer->GetAPIObject());

				// get joint buffer
				idJointBuffer jointBuffer;
				if (!vertexCache.GetJointBuffer(ds->jointCache, &jointBuffer)) {
					idLib::Warning("BackEnd ComputeSurfaceBones, jointBuffer == NULL");
					return;
				}

				DX12Rendering::Geometry::JointBuffer* const apiJointBuffer = static_cast<DX12Rendering::Geometry::JointBuffer*>(jointBuffer.GetAPIObject());

				// Update the vert structure.
				const UINT bufferOffset = dxRenderer.ComputeSurfaceBones(apiVertexBuffer, vertOffset, outVertexBufferOffset, vertSize, apiJointBuffer, jointBuffer.GetOffset());

				if (raytracedEnabled && ds->material && ds->material->SurfaceCastsShadow() /* Only allow shadow casting surfaces */)
				{
					const dxHandle_t handle = i;
					const UINT entityIndex = i << 16;

					DX12Rendering::RaytracingGeometryArgument dxArguments = {};
					dxArguments.vertexHandle = ds->ambientCache;
					dxArguments.vertCounts = vertIndexCount;
					dxArguments.vertexOffset = outVertexBufferOffset;

					dxArguments.indexHandle = ds->indexCache;
					dxArguments.indexCounts = ds->numIndexes;

					dxArguments.jointsHandle = ds->jointCache;

					// Add to the list of entities
					rtGeometry.push_back(dxArguments);
				}

				outVertexBufferOffset += bufferOffset;
			}
		}

		// If there are any dynamic BLAS objects being created. Finish them here.
		if (rtGeometry.size() > 0)
		{
			// Add the BLAS and TLAS
			dxRenderer.DXR_UpdateBLAS(blasHandle, std::string("DynamicBLAS: %d", blasHandle).c_str(), false, rtGeometry.size(), rtGeometry.data());

			UINT instanceMask = DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_NONE;
			{
				// Calculate the instance mask
				if (true || lastEntity->staticShadowVolumes != nullptr || lastEntity->dynamicShadowVolumes != nullptr) // TODO: fix this
				{
					instanceMask |= DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_CAST_SHADOW;
				}
			}

			dxRenderer.DXR_AddBLASToTLAS(
				lastEntity->entityIndex,
				blasHandle,
				lastEntity->modelRenderMatrix[0],
				DX12Rendering::ACCELERATION_INSTANCE_TYPE::INSTANCE_TYPE_DYNAMIC,
				static_cast<DX12Rendering::ACCELLERATION_INSTANCE_MASK>(instanceMask)
			);
		}

		commandManager->Execute();
		dxRenderer.EndComputeSurfaceBones();
	}

	// Update/Create the BLASa and create a list for the TLAS structure 
	UINT blasResult = 0;
	if (raytracedEnabled) {
		viewEntity_t* viewEntity = viewDef->viewEntitys;
		for (; viewEntity != NULL; viewEntity = viewEntity->next)
		{
			if (viewEntity->blasIndex > 0) // Dynamic objects will not set this value, so we know it's static.
			{
				UINT instanceMask = DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_NONE;
				{
					// Calculate the instance mask
					if (true || viewEntity->staticShadowVolumes != nullptr || viewEntity->dynamicShadowVolumes != nullptr) // TODO: Fix this
					{
						instanceMask |= DX12Rendering::ACCELLERATION_INSTANCE_MASK::INSTANCE_MASK_CAST_SHADOW;
					}
				}

				dxRenderer.DXR_AddBLASToTLAS(
					viewEntity->entityIndex,
					viewEntity->blasIndex,
					viewEntity->modelRenderMatrix[0],
					DX12Rendering::ACCELERATION_INSTANCE_TYPE::INSTANCE_TYPE_STATIC,
					static_cast<DX12Rendering::ACCELLERATION_INSTANCE_MASK>(instanceMask));
			}
		}

		blasResult = dxRenderer.DXR_UpdatePendingBLAS();
	}
}

void RB_DrawViewInternal(const viewDef_t* viewDef, const int stereoEye) {
	renderLog.OpenBlock("RB_DrawViewInternal");

	//-------------------------------------------------
	// guis can wind up referencing purged images that need to be loaded.
	// this used to be in the gui emit code, but now that it can be running
	// in a separate thread, it must not try to load images, so do it here.
	//-------------------------------------------------
	drawSurf_t** drawSurfs = (drawSurf_t**)&viewDef->drawSurfs[0];
	const int numDrawSurfs = viewDef->numDrawSurfs;

	const bool raytracedEnabled = dxRenderer.IsRaytracingEnabled() && (backEnd.viewDef->viewEntitys != NULL /* Only can be used on 3d models */);
	const bool useGBuffer = raytracedEnabled;

	for (int i = 0; i < numDrawSurfs; i++) {
		const drawSurf_t* ds = viewDef->drawSurfs[i];
		if (ds->material != NULL) {
			const_cast<idMaterial*>(ds->material)->EnsureNotPurged();
		}
	}

	//-------------------------------------------------
	// RB_BeginDrawingView
	//
	// Any mirrored or portaled views have already been drawn, so prepare
	// to actually render the visible surfaces for this view
	//
	// clear the z buffer, set the projection matrix, etc
	//-------------------------------------------------

	// set the window clipping
	GL_Viewport(viewDef->viewport.x1,
		viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1);

	// the scissor may be smaller than the viewport for subviews
	GL_Scissor(backEnd.viewDef->viewport.x1 + viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + viewDef->scissor.y1,
		viewDef->scissor.x2 + 1 - viewDef->scissor.x1,
		viewDef->scissor.y2 + 1 - viewDef->scissor.y1);
	backEnd.currentScissor = viewDef->scissor;

	backEnd.glState.faceCulling = -1;		// force face culling to set next time

	// ensures that depth writes are enabled for the depth clear
	GL_State(GLS_DEFAULT); //TODO: We need to properly implement this

	// Clear the depth buffer and clear the stencil to 128 for stencil shadows as well as gui masking
	GL_Clear(false, true, true, STENCIL_SHADOW_TEST_VALUE, 0.0f, 0.0f, 0.0f, 0.0f); //TODO: We need to properly implement this.

	// Clear surfaces from the previous frame
	if(useGBuffer)
	{
		const float zeroClear[4] = { 0, 0, 0, 0 };
		const DX12Rendering::eRenderSurface clearSurfaces[] = {
			DX12Rendering::eRenderSurface::Position,
			DX12Rendering::eRenderSurface::Normal,
			DX12Rendering::eRenderSurface::FlatNormal,
			DX12Rendering::eRenderSurface::RaytraceShadowMask,
			DX12Rendering::eRenderSurface::Diffuse,
			DX12Rendering::eRenderSurface::Specular,
			DX12Rendering::eRenderSurface::Albedo,
			DX12Rendering::eRenderSurface::SpecularColor
		};

		auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT)->RequestNewCommandList();
		DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "ClearSurfaces");

		for (DX12Rendering::eRenderSurface surface : clearSurfaces)
		{
			auto renderSurface = DX12Rendering::GetSurface(surface);
			if (renderSurface->Exists())
			{
				commandList->ClearRTV(
					renderSurface->GetRtv(),
					zeroClear);
			}
		}
	}

	// normal face culling
	GL_Cull(CT_FRONT_SIDED);

	//------------------------------------
	// sets variables that can be used by all programs
	//------------------------------------
	{
		//
		// set eye position in global space
		//
		float parm[4];
		parm[0] = backEnd.viewDef->renderView.vieworg[0];
		parm[1] = backEnd.viewDef->renderView.vieworg[1];
		parm[2] = backEnd.viewDef->renderView.vieworg[2];
		parm[3] = 1.0f;
		SetVertexParm(RENDERPARM_GLOBALEYEPOS, parm); // rpGlobalEyePos

		if (raytracedEnabled)
		{			
			float fov[4]; // { xmin, ymin, xmax, ymax }
			const float zNear = (viewDef->renderView.cramZNear) ? (r_znear.GetFloat() * 0.25f) : r_znear.GetFloat();

			fov[3] = zNear * tan(viewDef->renderView.fov_y * idMath::PI / 360.0f); // ymax
			fov[1] = -fov[3]; // ymin

			fov[2] = zNear * tan(viewDef->renderView.fov_x * idMath::PI / 360.0f); // xmax
			fov[0] = -fov[2]; // xmain

			dxRenderer.DXR_SetRenderParam(DX12Rendering::dxr_renderParm_t::RENDERPARM_FOV, fov);

			parm[3] = zNear;
			dxRenderer.DXR_SetRenderParam(DX12Rendering::dxr_renderParm_t::RENDERPARM_GLOBALEYEPOS, parm); // rpGlobalEyePos
		}

		// sets overbright to make world brighter
		// This value is baked into the specularScale and diffuseScale values so
		// the interaction programs don't need to perform the extra multiply,
		// but any other renderprogs that want to obey the brightness value
		// can reference this.
		float overbright = r_lightScale.GetFloat() * 0.5f;
		parm[0] = overbright;
		parm[1] = overbright;
		parm[2] = overbright;
		parm[3] = overbright;
		SetFragmentParm(RENDERPARM_OVERBRIGHT, parm);

		// Set Projection Matrix
		float projMatrixTranspose[16];
		R_MatrixTranspose(backEnd.viewDef->projectionMatrix, projMatrixTranspose);
		SetVertexParms(RENDERPARM_PROJMATRIX_X, projMatrixTranspose, 4);

		{
			// Setup viewport coordniates for screenspace shadows. 
			// TODO: Eventually add the x and y as needed
			//int x = backEnd.viewDef->viewport.x1;
			//int y = backEnd.viewDef->viewport.y1;
			int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
			int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

			// screen power of two correction factor (no longer relevant now)
			float screenCorrectionParm[4];
			screenCorrectionParm[0] = 1.0f;
			screenCorrectionParm[1] = 1.0f;
			screenCorrectionParm[2] = 0.0f;
			screenCorrectionParm[3] = 1.0f;
			SetFragmentParm(RENDERPARM_SCREENCORRECTIONFACTOR, screenCorrectionParm); // rpScreenCorrectionFactor

			// window coord to 0.0 to 1.0 conversion
			float windowCoordParm[4];
			windowCoordParm[0] = 1.0f / w;
			windowCoordParm[1] = 1.0f / h;
			windowCoordParm[2] = 0.0f;
			windowCoordParm[3] = 1.0f;
			SetFragmentParm(RENDERPARM_WINDOWCOORD, windowCoordParm); // rpWindowCoord
		}

		if (raytracedEnabled)
		{
			// Set the inverse projection matrix
			idRenderMatrix inverseProjection, inverseProjectionTranspose; //Mark start here. We may need to cut our clip off by a variable.
			/*idRenderMatrix::CreateInverseProjectionMatrix(
				backEnd.viewDef->renderView.fov_y, backEnd.viewDef->viewport.zmin,
				backEnd.viewDef->viewport.zmax, backEnd.viewDef->viewport.GetWidth() / backEnd.viewDef->viewport.GetHeight(),
				inverseProjection);
			idRenderMatrix::Transpose(inverseProjection, inverseProjectionTranspose);*/
			const idRenderMatrix projectionMatrix(
				projMatrixTranspose[0], projMatrixTranspose[1], projMatrixTranspose[2], projMatrixTranspose[3],
				projMatrixTranspose[4], projMatrixTranspose[5], projMatrixTranspose[6], projMatrixTranspose[7],
				projMatrixTranspose[8], projMatrixTranspose[9], projMatrixTranspose[10], projMatrixTranspose[11],
				projMatrixTranspose[12], projMatrixTranspose[13], projMatrixTranspose[14], projMatrixTranspose[15]
			);
			idRenderMatrix::Inverse(projectionMatrix, inverseProjection);
			dxRenderer.DXR_SetRenderParams(DX12Rendering::dxr_renderParm_t::RENDERPARM_INVERSE_PROJMATRIX_X, inverseProjection[0], 4);


			// Set the Inverse View Matrix
			idRenderMatrix* viewRenderMatrix = (idRenderMatrix*)backEnd.viewDef->worldSpace.modelViewMatrix;
			idRenderMatrix inverseViewRenderMatrix, transposeViewRenderMatrix;
			//idRenderMatrix::CreateFromOriginAxis(-backEnd.viewDef->renderView.vieworg, backEnd.viewDef->renderView.viewaxis, inverseViewRenderMatrix);
			idRenderMatrix::Inverse(*viewRenderMatrix, inverseViewRenderMatrix);
			idRenderMatrix::Transpose(inverseViewRenderMatrix, transposeViewRenderMatrix);
			dxRenderer.DXR_SetRenderParams(DX12Rendering::dxr_renderParm_t::RENDERPARM_INVERSE_VIEWMATRIX_X, transposeViewRenderMatrix[0], 4);
		}
	}

	RB_CalculateDynamicObjects(viewDef, raytracedEnabled);

	// if we are just doing 2D rendering, no need to fill the depth buffer
	bool raytraceUpdated = false;
	if (backEnd.viewDef->viewEntitys != NULL) {
		//-------------------------------------------------
		// fill the depth buffer and clear color buffer to black except on subviews
		//-------------------------------------------------
		RB_FillDepthBufferFast(drawSurfs, numDrawSurfs);

		if (raytracedEnabled)
		{
			raytraceUpdated = true;
			dxRenderer.DXR_UpdateAccelerationStructure();

			// Build our light list
			dxRenderer.DXR_SetupLights(backEnd.viewDef->viewLights, backEnd.viewDef->worldSpace.modelMatrix);
		}

		if (useGBuffer)
		{
			// Fill the GBuffer
			const DX12Rendering::Commands::FenceValue fence = RB_DrawGBuffer(drawSurfs, numDrawSurfs);

			DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->InsertFenceWait(fence);

			// Copy the depth buffer to a texture
			auto position = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Position);
			//viewDepth->fence.Wait();

			// TODO: Make a system to perform multiple copies
			DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();
			DX12Rendering::TextureBuffer* positionTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::POSITION);
			position->CopySurfaceToTexture(positionTexture, textureManager);

			// Copy the albedo
			auto albedo = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Albedo);
			DX12Rendering::TextureBuffer* albedoTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::ALBEDO);
			albedo->CopySurfaceToTexture(albedoTexture, textureManager);

			// Copy the specular
			auto specular = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::SpecularColor);
			DX12Rendering::TextureBuffer* specularTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::SPECULAR_COLOR);
			specular->CopySurfaceToTexture(specularTexture, textureManager);

			// Copy the flat normal map to a texture
			auto normalFlatMap = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::FlatNormal);
			DX12Rendering::TextureBuffer* normalFlatTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::WORLD_FLAT_NORMALS);
			normalFlatMap->CopySurfaceToTexture(normalFlatTexture, textureManager);

			// Copy the normal map to a texture
			auto normalMap = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Normal);
			DX12Rendering::TextureBuffer* normalTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::WORLD_NORMALS);
			normalMap->CopySurfaceToTexture(normalTexture, textureManager);

			DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->InsertFenceSignal();

			// TODO: Find a better place to reset this.
			DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->Reset();
		}
	}

	raytraceUpdated = raytraceUpdated && dxRenderer.DXR_CastRays(); // TODO: wait on the previous copy fence before casting the rays.
	if (raytraceUpdated)
	{
		//-------------------------------------------------
		// Cast rays into the scene
		//-------------------------------------------------

		//-------------------------------------------------
		// Copy the raytraced buffer to view
		//-------------------------------------------------
		dxRenderer.DXR_DenoiseResult();
		RB_DrawCombinedGBufferResults();
	}

	//-------------------------------------------------
	// main light renderer
	//-------------------------------------------------
	RB_DrawInteractions(raytracedEnabled);
	//dxRenderer.ExecuteCommandList();
	//dxRenderer.ResetCommandList();

	//-------------------------------------------------
	// now draw any non-light dependent shading passes
	//-------------------------------------------------
	int processed = 0;
	if (!r_skipShaderPasses.GetBool()) {
		DX12Rendering::RenderPassBlock renderPassBlock("Draw_ShaderPasses", DX12Rendering::Commands::DIRECT);

		renderLog.OpenMainBlock(MRB_DRAW_SHADER_PASSES);
		float guiScreenOffset;
		if (viewDef->viewEntitys != NULL) {
			// guiScreenOffset will be 0 in non-gui views
			guiScreenOffset = 0.0f;
		}
		else {
			guiScreenOffset = stereoEye * viewDef->renderView.stereoScreenSeparation;
		}
		processed = RB_DrawShaderPasses(drawSurfs, numDrawSurfs, guiScreenOffset, stereoEye);
		renderLog.CloseMainBlock();
	}

	//-------------------------------------------------
	// fog and blend lights, drawn after emissive surfaces
	// so they are properly dimmed down
	//-------------------------------------------------
	if (!raytraceUpdated) // This will be run by the raytraced lighting
	{
		RB_FogAllLights();
	}

	//-------------------------------------------------
		// capture the depth for the motion blur before rendering any post process surfaces that may contribute to the depth
		//-------------------------------------------------
	if (r_motionBlur.GetInteger() > 0) {
		const idScreenRect& viewport = backEnd.viewDef->viewport;
		globalImages->currentDepthImage->CopyDepthbuffer(viewport.x1, viewport.y1, viewport.GetWidth(), viewport.GetHeight());
	}

	//-------------------------------------------------
	// now draw any screen warping post-process effects using _currentRender
	//-------------------------------------------------
	if (processed < numDrawSurfs && !r_skipPostProcess.GetBool()) {
		int x = backEnd.viewDef->viewport.x1;
		int y = backEnd.viewDef->viewport.y1;
		int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
		int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

		RENDERLOG_PRINTF("Resolve to %i x %i buffer\n", w, h);

		GL_SelectTexture(0);

		// resolve the screen
		globalImages->currentRenderImage->CopyFramebuffer(x, y, w, h);
		backEnd.currentRenderCopied = true;

		// RENDERPARM_SCREENCORRECTIONFACTOR amd RENDERPARM_WINDOWCOORD overlap
		// diffuseScale and specularScale

		// screen power of two correction factor (no longer relevant now)
		float screenCorrectionParm[4];
		screenCorrectionParm[0] = 1.0f;
		screenCorrectionParm[1] = 1.0f;
		screenCorrectionParm[2] = 0.0f;
		screenCorrectionParm[3] = 1.0f;
		SetFragmentParm(RENDERPARM_SCREENCORRECTIONFACTOR, screenCorrectionParm); // rpScreenCorrectionFactor

		// window coord to 0.0 to 1.0 conversion
		float windowCoordParm[4];
		windowCoordParm[0] = 1.0f / w;
		windowCoordParm[1] = 1.0f / h;
		windowCoordParm[2] = 0.0f;
		windowCoordParm[3] = 1.0f;
		SetFragmentParm(RENDERPARM_WINDOWCOORD, windowCoordParm); // rpWindowCoord

		// render the remaining surfaces
		renderLog.OpenMainBlock(MRB_DRAW_SHADER_PASSES_POST);
		//RB_DrawShaderPasses(drawSurfs + processed, numDrawSurfs - processed, 0.0f /* definitely not a gui */, stereoEye);
		renderLog.CloseMainBlock();
	}	

#ifdef _DEBUG
	if (backEnd.viewDef->viewEntitys != NULL) {
		//dxRenderer.CopyDebugResultToDisplay();
	}
#endif

	renderLog.CloseBlock();
}

void RB_DrawView(const void* data, const int stereoEye) {
	const drawSurfsCommand_t* cmd = (const drawSurfsCommand_t*)data;
	backEnd.viewDef = cmd->viewDef;

	backEnd.currentRenderCopied = false;

	// if there aren't any drawsurfs, do nothing
	if (!backEnd.viewDef->numDrawSurfs) {
		return;
	}

	// skip render bypasses everything that has models, assuming
	// them to be 3D views, but leaves 2D rendering visible
	if (r_skipRender.GetBool() && backEnd.viewDef->viewEntitys) {
		return;
	}

	if (r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys) {
		// TODO: Implement skipping the render context.
	}

	backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

	//TODO: Implement
	// RB_ShowOverdraw();

	// render the scene
	RB_DrawViewInternal(cmd->viewDef, stereoEye);

	//TODO: Implemnt
	//RB_MotionBlur();

	// restore the context for 2D drawing if we were stubbing it out
	if (r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys) {
		//GLimp_ActivateContext();
		GL_SetDefaultState();
	}
}

void RB_CopyRender(const void* data) {
	// TODO: Copy the render
}

void RB_PostProcess(const void* data) {
	// TODO: Perform post processing.
}

void RB_ExecuteBackEndCommands(const emptyCommand_t* cmds) {
	int c_draw3d = 0;
	int c_draw2d = 0;
	int c_setBuffers = 0;
	int c_copyRenders = 0;

	renderLog.StartFrame();

	if (cmds->commandId == RC_NOP && !cmds->next) {
		return;
	}

	// TODO: Check rendering in stereo mode.

	uint64 backEndStartTime = Sys_Microseconds();

	dxRenderer.BeginDraw();
	GL_SetDefaultState();

	for (; cmds != NULL; cmds = (const emptyCommand_t*)cmds->next) {
		switch (cmds->commandId) {
		case RC_NOP:
			break;

		case RC_DRAW_VIEW_GUI:
			RB_DrawView(cmds, 0);
			c_draw2d++;
			break;

		case RC_DRAW_VIEW_3D:
			{
				RB_DrawView(cmds, 0);
			}

			if (((const drawSurfsCommand_t*)cmds)->viewDef->viewEntitys) {
				c_draw3d++;
			}
			break;
		case RC_SET_BUFFER:
			c_setBuffers++;
			RB_SetBuffer();
			break;
		case RC_COPY_RENDER:
			RB_CopyRender(cmds);
			c_copyRenders++;
			break;
		case RC_POST_PROCESS:
			RB_PostProcess(cmds);
			break;
		default:
			common->Error("RB_ExecuteBackEndCommands: bad commandId");
			break;
		}
	}

	// TODO: reset the color mask

	// stop rendering on this thread
	dxRenderer.EndDraw();	

	const uint64 backEndFinishTime = Sys_Microseconds();
	backEnd.pc.totalMicroSec = backEndFinishTime - backEndStartTime;

	if (r_debugRenderToTexture.GetInteger() == 1) {
		common->Printf("3d: %i, 2d: %i, SetBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_copyRenders, backEnd.pc.c_copyFrameBuffer);
		backEnd.pc.c_copyFrameBuffer = 0;
	}
	renderLog.EndFrame();
}
