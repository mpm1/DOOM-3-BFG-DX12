#include "functions.inc"
#include "bindless_textures.inc"

// Minimum distance from the surface, based on the normal vector, where we begin to cast rays.
static const float k_minSurfaceDistance = 0.01;

// Plane used for ray calculations
typedef float4 Plane; // xyz = normal, w = distance. Used to define a plane

Plane GeneratePlane(in float3 normal, in float3 pointOnPlane)
{
    Plane plane;

    plane.xyz = normalize(normal);
    plane.w = -dot3(plane.xyz, pointOnPlane);

    return plane;
}

float PlaneDotVector(in Plane f, in float3 v)
{
    return dot3(f.xyz, v.xyz);
}

float PlaneDotPoint(in Plane f, in float3 p)
{
    return dot3(f.xyz, p.xyz) + f.w;
}

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

#define LIGHT_FLAG_CASTS_SHADOWS 0x00000001

#define INSTANCE_MASK_CAST_SHADOW 0x01
#define INSTANCE_MASK_SKYBOX 0x02

struct SceneLight {
    uint    sceneIndex;
    uint    shadowMask;
    uint    falloffIndex;
    uint    projectionIndex;

    uint    flags;
    uint    pad1;
    uint    pad2;
    float   emissiveRadius;

    float4   color;

    float4  center;
    
    float4  scissor; // Light scissor window {left, top, right, bottom}

    // Used to calculate the angle and falloff for the light influence.
    float4  projectionS;
    float4  projectionT;
    float4  projectionQ;
    float4  falloffS;
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

#define MAX_SHADOW_LIGHTS 128

struct RaytracedSceneConstants {
    RenderParameters rp;

    uint lightCount;
    float noiseOffset;
    uint diffuseTextureIndex;
    uint specularTexureIndex;

    uint depthTextureIndex;
	uint flatNormalIndex;
    uint normalIndex;
    uint raysPerLight;

    SceneLight lights[MAX_SHADOW_LIGHTS];
};

inline float4 GetInverseProjection(float4 location, in RaytracedSceneConstants constants)
{
    return float4(
        dot4(location, constants.rp.invProjMatrixX),
        dot4(location, constants.rp.invProjMatrixY),
        dot4(location, constants.rp.invProjMatrixZ),
        dot4(location, constants.rp.invProjMatrixW)
    );
}

inline float4 GetInverseView(float4 location, in RaytracedSceneConstants constants)
{
    return float4(
        dot4(location, constants.rp.invViewMatrixX),
        dot4(location, constants.rp.invViewMatrixY),
        dot4(location, constants.rp.invViewMatrixZ),
        dot4(location, constants.rp.invViewMatrixW)
    );
}

inline float4 GetWorldPointFromDepth(float depth, float2 screenPosition, in RaytracedSceneConstants constants)
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

SamplerState    pointSampler : register(s0, space0); // Replace this with Sampler that will grab the exact point information from the shader.
SamplerState    projectionSampler : register(s1, space0);