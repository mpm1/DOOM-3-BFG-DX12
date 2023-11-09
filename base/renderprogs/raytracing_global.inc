// Ray payload. Contains:
// Diffuse color
// Emmissive color
// Basic infor for a hit.
struct ShadowHitInfo
{
    float maxDistance;
    float intensity; // The light intensity
};

// Attributes output by the raytracing when hitting a surface:
struct Attributes
{
  float4	location; // Location of the hit.
};

struct SceneLight {
    uint    sceneIndex;
    uint    shadowMask;
    uint    pad1;
    uint    pad2;

    float3  location;
    float   radius;
    
    float4  scissor; // Light scissor window {left, top, right, bottom}
};

struct RenderParameters {
	float4 globalEyePos;
    float4 viewport; // {left, top, right, bottom}
    float4 scissor; // {left, top, right, bottom}

    float4 invViewMatrixX;
 	float4 invViewMatrixY;
 	float4 invViewMatrixZ;
 	float4 invViewMatrixW;

 	float4 invProjMatrixX;
 	float4 invProjMatrixY;
 	float4 invProjMatrixZ;
 	float4 invProjMatrixW;

};

#define MAX_SHADOW_LIGHTS 32 // Only 32 so that we can mask each result to a bit.

struct SceneConstants {
    RenderParameters rp;

    uint lightCount;
    uint pad1;
    uint pad2;
    uint pad3;

    SceneLight lights[MAX_SHADOW_LIGHTS];
};

static float dot2( float2 a, float2 b ) { return dot( a, b ); }
static float dot3( float3 a, float3 b ) { return dot( a, b ); }
static float dot3( float3 a, float4 b ) { return dot( a, b.xyz ); }
static float dot3( float4 a, float3 b ) { return dot( a.xyz, b ); }
static float dot3( float4 a, float4 b ) { return dot( a.xyz, b.xyz ); }
static float dot4( float4 a, float4 b ) { return dot( a, b ); }
static float dot4( float2 a, float4 b ) { return dot( float4( a, 0, 1 ), b ); }

inline float4 GetInverseProjection(float4 location, in SceneConstants constants)
{
    return float4(
        dot4(location, constants.rp.invProjMatrixX),
        dot4(location, constants.rp.invProjMatrixY),
        dot4(location, constants.rp.invProjMatrixZ),
        dot4(location, constants.rp.invProjMatrixW)
    );
}

inline float4 GetInverseView(float4 location, in SceneConstants constants)
{
    return float4(
        dot4(location, constants.rp.invViewMatrixX),
        dot4(location, constants.rp.invViewMatrixY),
        dot4(location, constants.rp.invViewMatrixZ),
        dot4(location, constants.rp.invViewMatrixW)
    );
}

inline float4 GetWorldPointFromDepth(float depth, float2 screenPosition, in SceneConstants constants)
{
    screenPosition.y = 1.0 - screenPosition.y; // Invert the y for directx style coordinates.

    float4 clipPosition = float4(screenPosition * 2.0 - 1.0, depth, 1.0);
    float4 viewPosition = GetInverseProjection(clipPosition, constants);
    
    viewPosition /= viewPosition.w;

    float4 worldPosition = GetInverseView(viewPosition, constants);

    return worldPosition;
}

uint4 ConvertShadowmapMaskToVector(uint shadowMask)
{
    uint4 result;

    result.a = (shadowMask >> 24) & 0x000000FF;
	result.b = (shadowMask >> 16) & 0x000000FF;
	result.g = (shadowMask >> 8) & 0x000000FF;
	result.r = shadowMask & 0xFF;

    return result;
}

SamplerState    pointSampler : register(s0, space0); // Sampler that will grab the exact point information from the shader.