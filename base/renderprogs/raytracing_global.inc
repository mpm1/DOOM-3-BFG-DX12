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

    float4   emmisiveRadius;
    float4   color;

    float3  location;
    float   radius;
    
    float4  scissor; // Light scissor window {left, top, right, bottom}
};

struct RenderParameters {
	float4 globalEyePos; // {eye.x, eye.y, eye.z, zNear plane}
    float4 fov; // { min fovX, min fovY, max fovX, max fovY }  
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
    float4 viewPosition = float4(
        lerp(constants.rp.fov.x, constants.rp.fov.z, screenPosition.x),
        lerp(constants.rp.fov.y, constants.rp.fov.w, 1.0 - screenPosition.y),
        -constants.rp.globalEyePos.w, // We make this negitive due to the system using OpenGL coordinates
        1.0
    );

    viewPosition.xyz = normalize(viewPosition.xyz) * depth;

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

// Temp random function. found here: https://thebookofshaders.com/10/
float random1(float2 st, float2 dotDir) {
    return frac(sin(dot(st.xy, dotDir)) * 43758.5453123);
}

float3 random3(float2 st) {
    return float3(
		random1(st, float2(12.9898, 78.233)),
		random1(st, float2(39.346, 11.135)),
		random1(st, float2(73.156, 52.235))
	);
}

SamplerState    pointSampler : register(s0, space0); // Replace this with Sampler that will grab the exact point information from the shader.