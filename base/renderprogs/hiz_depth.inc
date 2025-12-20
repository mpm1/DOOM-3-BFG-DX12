#include "bindless_textures.inc"

struct HiZDepthConstants
{
	uint width;
	uint height;
	uint mips;
	uint textureIndex;
};
ConstantBuffer<HiZDepthConstants> depthConstants : register(b2, space0); // Reusing the Surface Constants

float4 FindIntersectingDepthPoint(in float3 startUV, in float3 uvVector, in float resolution, in int steps, in float thickness)
{
	if(length(uvVector) == 0.0)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}

	Texture2D<float4> depthTexture = GetBindlessTexture4f(depthConstants.textureIndex);

	// Find the UV end point
	float useX = abs(uvVector.x) > abs(uvVector.y) ? 1.0 : 0.0;
	float3 endUV = float3(startUV);
	{
		float direction = lerp(uvVector.y, uvVector.x, useX);
		float distance = lerp(startUV.y, startUV.x, useX);

		if(direction > 0)
		{
			distance = 1.0 - distance;
		}

		endUV += (uvVector * 2);
	}

	float3 startFrag = float3(startUV / sceneConstants.rpWindowCoord.xy, startUV.z);
	float3 endFrag = float3(endUV.xy / sceneConstants.rpWindowCoord.xy, endUV.z);
	
	const float fragMult = startFrag.z * endFrag.z;
	float3 deltaFrag = endFrag - startFrag;
	float delta = lerp(deltaFrag.y, deltaFrag.x, useX);

	float2 increment = deltaFrag.xy / max(delta, EPSILON);

	float search0 = 0.0;
	float search1 = 0.0;

	float viewDistance = startFrag.z;
	float depth = thickness;
	
	float2 frag = startFrag.xy + float2(0.5, 0.5); // Start in the middle of the fragment.
	float2 uv = frag * sceneConstants.rpWindowCoord.xy;
	uv.y = 1.0 - uv.y;

	int hit = 0;

	for (int i = 0; i < int(delta); ++i)
	{
		frag += increment;
		uv = frag * sceneConstants.rpWindowCoord.xy;

		bool inRange = all(uv >= 0.0f) && all(uv <= 1.0f);
		if (!inRange)
		{
			break;
		}

		uv.y = 1.0 - uv.y;

		float2 deltaFragTraverse = (frag - startFrag.xy) / max(deltaFrag.xy, 0.0001);
		search1 = lerp(deltaFragTraverse.y, deltaFragTraverse.x, useX);

		viewDistance = fragMult / lerp(endFrag.z, startFrag.z, search1);
		depth = viewDistance - depthTexture.Sample(pointSampler, uv).r;

		if(depth > 0 && depth < thickness)
		{
			++hit;
			break;
		}
		else
		{
			search0 = search1;
		}
	}

	// Pinpoint the hit
	steps *= hit; // If 0 we don't need to work any further.
	search1 = search0 + ((search1 - search0) / 2.0);

	for(int i = 0; i < steps; ++i)
	{
		frag = lerp(startFrag.xy, endFrag.xy, search1);
		uv = frag * sceneConstants.rpWindowCoord.xy;
		uv.y = 1.0 - uv.y;

		viewDistance = fragMult / lerp(endFrag.z, startFrag.z, search1);
		depth = viewDistance - depthTexture.Sample(pointSampler, uv).r;

		if (depth > 0 && depth < thickness) {
			hit = 2;
			search1 = search0 + ((search1 - search0) / 2);
		} else {
			float temp = search1;
			search1 = search1 + ((search1 - search0) / 2);
			search0 = temp;
		}
	}

	if(hit != 2 || search1 < 0.02)
	{
		return float4(0, 0, 0, 0);
	}

	return float4(uv, 0.0, 1.0 - search1);
}

