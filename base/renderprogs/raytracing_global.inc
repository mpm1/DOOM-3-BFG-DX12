// Ray payload. Contains:
// Diffuse color
// Emmissive color
// Basic infor for a hit.
struct HitInfo
{
    float maxDistance;
	float3 diffuse;
};

// Attributes output by the raytracing when hitting a surface:
struct Attributes
{
  float4	location; // Location of the hit.
};

struct SceneConstants {
	float4 rpGlobalEyePos;
    float4 rpViewport; // {left, top, right, bottom}
    float4 rpScissor; // {left, top, right, bottom}

    float4 rpInvViewMatrixX;
 	float4 rpInvViewMatrixY;
 	float4 rpInvViewMatrixZ;
 	float4 rpInvViewMatrixW;

 	float4 rpInvProjMatrixX;
 	float4 rpInvProjMatrixY;
 	float4 rpInvProjMatrixZ;
 	float4 rpInvProjMatrixW;
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
        dot4(location, constants.rpInvProjMatrixX),
        dot4(location, constants.rpInvProjMatrixY),
        dot4(location, constants.rpInvProjMatrixZ),
        dot4(location, constants.rpInvProjMatrixW)
    );
}

inline float4 GetInverseView(float4 location, in SceneConstants constants)
{
    return float4(
        dot4(location, constants.rpInvViewMatrixX),
        dot4(location, constants.rpInvViewMatrixY),
        dot4(location, constants.rpInvViewMatrixZ),
        dot4(location, constants.rpInvViewMatrixW)
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

SamplerState    pointSampler : register(s0, space0); // Sampler that will grab the exact point information from the shader.