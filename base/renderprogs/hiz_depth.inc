#include "bindless_textures.inc"

struct HiZDepthConstants
{
	uint width;
	uint height;
	uint startMip;
	uint endMip;

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

float4 FindIntersectingDepthPoint(in float3 startUV, in float3 uvVector, in float thickness)
{
	if(length(uvVector) == 0.0)
	{
		return float4(0.0, 0.0, 1.0, 0.0);
	}

	Texture2D<float4> depthTexture = GetBindlessTexture4f(depthConstants.textureIndex);

	const uint maxLevel = depthConstants.endMip;
	uint level = min(depthConstants.startMip + 1, maxLevel);
	
	// Find the UV end point
	float3 endUV = GetEndUV(startUV, uvVector);

	float3 startFrag = float3(startUV.xy / sceneConstants.rpWindowCoord.xy, startUV.z);
	float3 endFrag = float3(endUV.xy / sceneConstants.rpWindowCoord.xy, endUV.z);
	
	const float fragMult = startFrag.z * endFrag.z;
	float3 deltaFrag = endFrag - startFrag;
	bool useX = abs(deltaFrag.x) > abs(deltaFrag.y);
	float delta = useX ? abs(deltaFrag.x) : abs(deltaFrag.y);

	float2 increment = deltaFrag.xy / max(delta / float(1 << level), EPSILON); // Only valid if Level starts at 1

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

	[loop]
	while(search1 <= 1.0)
	{
		float2 frag = lastFrag + increment;
		float2 uv = frag * sceneConstants.rpWindowCoord.xy;
		
		float evalValue = useX ? uv.x : uv.y;
		if (evalValue < 0.0 || evalValue > 1.0)
		{
			break;
		}

		uv.y = 1.0 - uv.y;

		float2 currentFragDelta = abs(frag - startFrag.xy);
		search1 = (useX ? currentFragDelta.x : currentFragDelta.y) / max(delta, EPSILON);

		viewDistance = fragMult / lerp(endFrag.z, startFrag.z, search1);
		int3 sampleLocation = int3(uv * float2(depthConstants.width >> level, depthConstants.height >> level), level);

		depth = viewDistance - depthTexture.Load(sampleLocation).r;//SampleLevel(pointSampler, uv, level).r;

		if(level > depthConstants.startMip)
		{
			if(depth < thickness)
			{
				--level;
				increment = deltaFrag.xy / max(delta / float(1 << level), EPSILON);
				continue;
			}
		}
		else if(depth > 0 && depth < thickness)
		{
			++hit;

			lastUv = uv;
			lastFrag = frag;

			break;
		}

		if(level < maxLevel)
		{
			++level;
		}
		increment = deltaFrag.xy / max(delta / float(1 << level), EPSILON);

		search0 = search1;
		lastUv = uv;
		lastFrag = frag;
	}

	if(hit != 1 || length(startFrag.xy - lastFrag.xy) < 4.0)
	{
		return float4(0, 0, 0.5, 0);
	}

	return float4(lastUv, 0.0, 1.0 - search1);
}

