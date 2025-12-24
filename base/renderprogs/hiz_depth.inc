#include "bindless_textures.inc"

struct HiZDepthConstants
{
	uint width;
	uint height;
	uint mips;
	uint textureIndex;
};
ConstantBuffer<HiZDepthConstants> depthConstants : register(b2, space0); // Reusing the Surface Constants

float3 GetEndUV(in float3 startUV, in float3 uvVector)
{
	float3 uvEnd = startUV;
	float distance = -1.0;

	float checkVal = uvVector.x > 0.0 ? 1.0 : 0.0;

	if(uvVector.x != 0.0)
	{
		distance = (checkVal - startUV.x) / uvVector.x;

		uvEnd = startUV + (uvVector * distance);

		if(uvEnd.y < 0.0f || uvEnd.y > 1.0f)
		{
			distance = -2;
		}
	}

	if (distance <= 0 && uvVector.y != 0)
	{
		checkVal = uvVector.y > 0.0 ? 1.0 : 0.0;
		distance = (checkVal - startUV.y) / uvVector.y;

		uvEnd = startUV + (uvVector * distance);
	}

	return uvEnd;
}

float4 FindIntersectingDepthPoint(in float3 startUV, in float3 uvVector, in float resolution, in int steps, in float thickness)
{
	if(length(uvVector) == 0.0)
	{
		return float4(0.0, 0.0, 1.0, 0.0);
	}

	Texture2D<float4> depthTexture = GetBindlessTexture4f(depthConstants.textureIndex);

	const uint maxLevel = depthConstants.mips - 1;
	uint level = 1;
	
	// Find the UV end point
	float3 endUV = GetEndUV(startUV, uvVector);

	float3 startFrag = float3(startUV / sceneConstants.rpWindowCoord.xy, startUV.z);
	float3 endFrag = float3(endUV.xy / sceneConstants.rpWindowCoord.xy, endUV.z);
	
	const float fragMult = startFrag.z * endFrag.z;
	float3 deltaFrag = endFrag - startFrag;
	float useX = abs(deltaFrag.x) > abs(deltaFrag.y) ? 1.0 : 0.0;
	float delta = (lerp(deltaFrag.y, deltaFrag.x, useX));

	float2 increment = deltaFrag.xy / max(delta / pow(2.0, level), EPSILON);

	if(length(increment) == 0)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}

	float search0 = 0.0;
	float search1 = 0.0;

	float viewDistance = startFrag.z;
	float depth = thickness;
	
	float2 lastFrag = startFrag.xy;
	float2 lastUv = lastFrag * sceneConstants.rpWindowCoord.xy;
	lastUv.y = 1.0 - lastUv.y;

	int hit = 0;

	while(search1 <= 1.0)
	{
		float2 frag = lastFrag + increment;
		float2 uv = frag * sceneConstants.rpWindowCoord.xy;
		
		bool inRange = all(uv >= 0.0f) && all(uv <= 1.0f);
		if (!inRange)
		{
			if(level == 0)
			{
				break;
			}

			--level;
			increment = deltaFrag.xy / max(delta / pow(2.0, level), EPSILON);
			continue;
		}

		uv.y = 1.0 - uv.y;

		float2 deltaFragTraverse = (frag - startFrag.xy) / max(deltaFrag.xy, EPSILON);
		search1 = lerp(deltaFragTraverse.y, deltaFragTraverse.x, useX);

		viewDistance = fragMult / lerp(endFrag.z, startFrag.z, search1);
		depth = viewDistance - depthTexture.SampleLevel(pointSampler, uv, level).r;

		if(level > 0)
		{
			if(depth < thickness)
			{
				--level;
				increment = deltaFrag.xy / max(delta / pow(2.0, level), EPSILON);
				continue;
			}
		}
		else
		{
			if(depth > 0 && depth < thickness)
			{
				++hit;

				lastUv = uv;
				lastFrag = frag;

				break;
			}
		}

		level = min(level + 1, maxLevel);
		increment = deltaFrag.xy / max(delta / pow(2.0, level), EPSILON);

		search0 = search1;
		lastUv = uv;
		lastFrag = frag;
	}

	// Pinpoint the hit
	/*steps *= hit; // If 0 we don't need to work any further.
	search1 = search0 + ((search1 - search0) / 2.0);

	for(int i = 0; i < steps; ++i)
	{
		float2 frag = lerp(startFrag.xy, endFrag.xy, search1);
		float2 uv = frag * sceneConstants.rpWindowCoord.xy;
		uv.y = 1.0 - uv.y;

		viewDistance = fragMult / lerp(endFrag.z, startFrag.z, search1);
		depth = viewDistance - depthTexture.SampleLevel(pointSampler, uv, 0).r;

		if (depth > 0 && depth < thickness) {
			hit = 2;
			search1 = search0 + ((search1 - search0) / 2);
		} else {
			float temp = search1;
			search1 = search1 + ((search1 - search0) / 2);
			search0 = temp;
		}

		lastUv = uv;
	}*/

	if(hit != 1 || length(startFrag.xy - lastFrag.xy) < 4.0)
	{
		return float4(0, 0, 0.5, 0);
	}

	return float4(lastUv, 0.0, 1.0 - search1);
}

