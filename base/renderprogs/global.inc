/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "functions.inc"

#define GROUP_THREADS 64

struct SceneConstants {
	float4 rpScreenCorrectionFactor;
	float4 rpWindowCoord;
	float4 rpDiffuseModifier;
	float4 rpSpecularModifier;

	float4 rpLocalLightOrigin;
	float4 rpLocalViewOrigin;

	float4 rpLightProjectionS;
	float4 rpLightProjectionT;
	float4 rpLightProjectionQ;
	float4 rpLightFalloffS;

	float4 rpBumpMatrixS;
	float4 rpBumpMatrixT;

	float4 rpDiffuseMatrixS;
	float4 rpDiffuseMatrixT;

	float4 rpSpecularMatrixS;
	float4 rpSpecularMatrixT;

	float4 rpVertexColorModulate;
	float4 rpVertexColorAdd;

	float4 rpColor;
	float4 rpViewOrigin;
	float4 rpGlobalEyePos;

	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;

	float4 rpModelMatrixX;
	float4 rpModelMatrixY;
	float4 rpModelMatrixZ;
	float4 rpModelMatrixW;

	float4 rpNormalMatrixX;
	float4 rpNormalMatrixY;
	float4 rpNormalMatrixZ;
	float4 rpNormalMatrixW;

	float4 rpProjectionMatrixX;
	float4 rpProjectionMatrixY;
	float4 rpProjectionMatrixZ;
	float4 rpProjectionMatrixW;

	float4 rpModelViewMatrixX;
	float4 rpModelViewMatrixY;
	float4 rpModelViewMatrixZ;
	float4 rpModelViewMatrixW;

	float4 rpTextureMatrixS;
	float4 rpTextureMatrixT;

	float4 rpTexGen0S;
	float4 rpTexGen0T;
	float4 rpTexGen0Q;
	float4 rpTexGen0Enabled;

	float4 rpTexGen1S;
	float4 rpTexGen1T;
	float4 rpTexGen1Q;
	float4 rpTexGen1Enabled;

	float4 rpWobbleSkyX;
	float4 rpWobbleSkyY;
	float4 rpWobbleSkyZ;

	float4 rpOverbright;
	float4 rpEnableSkinning;
	float4 rpAlphaTest;

	float4 rpMaterialProperties;

	// Not yet setup
	float4 rpPrevMVPmatrixX;
	float4 rpPrevMVPmatrixY;
	float4 rpPrevMVPmatrixZ;
	float4 rpPrevMVPmatrixW;
};
ConstantBuffer<SceneConstants> sceneConstants : register(b0, space0);

SamplerState baseSampler : register(s0, space0); 
SamplerState lightSampler : register(s1, space0); 
SamplerState pointSampler : register(s2, space0); 

#define VPOS WPOS
static float2 vposToScreenPosTexCoord( float2 vpos ) { return vpos.xy * sceneConstants.rpWindowCoord.xy; }

// TODO: Create root signatures based on shader functionality. For now all shaders will have access.
struct LightConstants {
	uint lightMask; // Mask used to represent the light in screenspace shaders. 0 means no reference to the sceenpace shadow map.
};
ConstantBuffer<LightConstants> lightConstants : register(b0, space2);

#ifdef USE_LIGHT
uint ConvertShadowmapToMask(uint4 shadowMapValue)
{
	uint result =	shadowMapValue.a << 24;
	result +=		shadowMapValue.b << 16;
	result +=		shadowMapValue.g << 8;
	result +=		shadowMapValue.r;

	return result;
}

float GetScreenspaceIntensity(uint4 shadowMapValue)
{
	if(lightConstants.lightMask == 0)
	{
		return 1.0;
	}
	else
	{
		uint shadowMask = ConvertShadowmapToMask(shadowMapValue);

		if((shadowMask & lightConstants.lightMask) == lightConstants.lightMask)
		{
			return 1.0;
		}
	}

	return 0.0;
}
#endif

#define BRANCH
#define IFANY
