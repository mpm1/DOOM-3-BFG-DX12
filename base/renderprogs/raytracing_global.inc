#include "functions.inc"
#include "bindless_textures.inc"
#include "geometry.inc"

// Copied from VertexCache.h
const int VERTCACHE_STATIC = 1;					// in the static set, not the per-frame set
const int VERTCACHE_SIZE_SHIFT = 1;
const int VERTCACHE_SIZE_MASK = 0x7fffff;		// 8 megs 
const int VERTCACHE_OFFSET_SHIFT = 24;
const int VERTCACHE_OFFSET_MASK = 0x1ffffff;	// 32 megs 
const int VERTCACHE_FRAME_SHIFT = 49;
const int VERTCACHE_FRAME_MASK = 0x7fff;		// 15 bits = 32k frames to wrap around

const int VERTEX_CACHE_ALIGN		= 32;
const int INDEX_CACHE_ALIGN			= 16;
const int JOINT_CACHE_ALIGN			= 16;

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
  float2	location; // Location of the hit.
};

#define LIGHT_FLAG_CASTS_SHADOWS (0x0001 << 0)
#define LIGHT_FLAG_POINT_LIGHT   (0x0001 << 1)
#define LIGHT_FLAG_AMBIENT_LIGHT (0x0001 << 2)
#define LIGHT_FLAG_FOG_LIGHT     (0x0001 << 3)

#define MAX_SCENE_LIGHTS 128

#define INSTANCE_MASK_CAST_SHADOW 0x01
#define INSTANCE_MASK_SKYBOX 0x02

struct SceneLight {
    uint    sceneIndex;
    uint    shadowMask;
    uint    falloffIndex;
    uint    projectionIndex;

    uint    flags;
    float   shadowStartDistance; // Distance from the light to start the shadow casting. 
    uint    pad2;
    float   emissiveRadius;

    float4   diffuseColor;
    float4   specularColor;

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

struct RaytracedSceneConstants {
    RenderParameters rp;

    uint lightCount;
    uint diffuseTextureIndex;
    uint specularTexureIndex;
    uint materialTextureIndex;

    uint positionTextureIndex;
	uint flatNormalIndex;
    uint normalIndex;
    uint raysPerLight;

    float noiseOffset;
	uint flatTangentIndex;
	uint pad1;
	uint pad2;
};

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH :register(t0);

// Storage for the geometry information
#define MAX_BLAS_GEOMETRY 2048

StructuredBuffer<RaytracingGeometryArgument>    geometry_srv : register(t1);
StructuredBuffer<BLASVertex>                    vertecies_srv : register(t2);
StructuredBuffer<BLASIndex>                     indecies_srv : register(t3);

inline uint GetByteOffsetFromHandle(in uint64_t bufferHandle)
{
    return int(bufferHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
}

inline RaytracingGeometryArgument GetGeometryArgument(in uint blasIndex, in uint geometryIndex)
{
    uint index = blasIndex + geometryIndex;

    if(index > MAX_BLAS_GEOMETRY)
    {
        // TODO: implement error
        index = 0;
    }

    return geometry_srv[index];
}

inline BLASVertex GetVertex(in uint blasIndex, in uint geometryIndex, in uint index1, in uint index2, in uint index3, in float2 coord)
{
    RaytracingGeometryArgument geometry = GetGeometryArgument(blasIndex, geometryIndex);
    uint indexOffset = GetByteOffsetFromHandle(geometry.indexHandle) / 2 + index1; // We devide by 2 because the indices are 16-bits (or two bytes) each
    uint vertOffset = GetByteOffsetFromHandle(geometry.vertexHandle) / sizeof(BLASVertex);

    uint checkIndex = UnpackBLASIndex(indexOffset, indecies_srv);
    BLASVertex vert1;// = vertecies_srv[vertOffset + checkIndex];
    //TODO: When we get back to GI, fix our calls to indecies_srv and vertecies_srv. These cause issues with the root signature
    vert1.normal = float3(0.0f, 0.0f, 1.0f); // Default normal, we will calculate it later
    // TODO: Add the dynamic vectors as well

    // TODO: calculate the connection between all points. We're just sending the first for testing 
    return vert1;
}

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

float3 CalculateFalloff(in SceneLight light, float4 worldPosition)
{
	float4 uv = dot4( worldPosition, light.falloffS );
	float4 falloff =  SampleBindlessTexture4fLevel(light.falloffIndex, uv.xy, 0);

	return falloff.rgb;
}

float3 GetLightSpacePosition(in SceneLight light,  in float3 worldPosition)
{
    // Given a light, calculate the position of the point with respect to the light in the world.
    //float3 location = worldPosition.xyz - light.center; //TODO: Realign the light to match the perspective angle of the point
   
    //return location;
    return worldPosition;
}

float3 CalculateProjection(in SceneLight light, in float4 location)
{
	float4 projection = float4(
		dot4(location, light.projectionS),
		dot4(location, light.projectionT),
		0.0f,
		dot4(location, light.projectionQ)
	);

    projection.xy /= projection.w; //This is done to match idtex2Dproj
    
	return SampleBindlessTexture4fLevel(light.projectionIndex, projection.xy, 1).rgb;//pow(SampleBindlessTexture4fLevel(light.projectionIndex, projection.xy, 0).rgb, 1 / 2.2); // Added some correction to match the original art
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

#define RAY_MIN_START_DISTANCE 0.001
#define RAY_MAX_START_DISTANCE 1.0

float StartDistanceFromSurface(in float3 worldPosition, in float3 eyePosition)
{
    float depth = distance(worldPosition, eyePosition);
	
    return lerp(RAY_MIN_START_DISTANCE, RAY_MAX_START_DISTANCE, depth / 100.0f);
}

// Light functions
// General BRDF functions
void GeneralBRDF(
    in float3 lightColor,
    in float3 diffuseColor,
    in float3 specularColor,
    in float NdotL,
    in float3 surfaceNormal,
    in float distribution, // The integral value over the surface for the light. This should usually be PI, but for our raytracing we'll set this to 1.0 and devide by the sample count later.'
    in float3 baseReflectivity, //TODO: Add this as a material property to the surface.
    in float roughness, //TODO: Add this as a material property to the surface.
    in float3 viewVector,
    in float3 halfVector,
    out float3 diffuseContribution,
    out float3 specularContribution)
{
    float3 fresnelFactor = CalculateFresnel(baseReflectivity, viewVector, halfVector);

    // Diffuse using the Lambertian Model (we already used L dot N for the light Color)
    diffuseContribution = lightColor * (diffuseColor * NdotL * (1.0 - fresnelFactor)) / distribution;

    // Using Cook-Torrance BRDF for specular reflection
    specularContribution = lightColor * specularColor * 
        calculateCookTorranceSpecularRefectivity(
            fresnelFactor, viewVector,
            surfaceNormal, halfVector,
            distribution, NdotL, roughness);
}

// Calculates the amount of specular and diffuse light hitting a surface.
// Return value is the intesity of light reflecting off the surface. An intensity of 0 or less means no light will be reflected on the surface.
float CalcluateLightImpact(
    in SceneLight light,
    in float3 worldPosition,
    in float3 viewVector, // Hit point to eye vector.
    in float3 surfaceNormal,
    in float3 lightVector,
    in float distribution, // The integral value over the surface for the light. This should usually be PI, but for our raytracing we'll set this to 1.0 and devide by the sample count later.'
    in float3 baseReflectivity, //TODO: Add this as a material property to the surface.
    in float roughness, //TODO: Add this as a material property to the surface.
    out float3 diffuseContribution,
    out float3 specularContribution
    )
{
    float NdotL = dot3(surfaceNormal, lightVector);
    float3 lightWorldPosition = GetLightSpacePosition(light, worldPosition);

    // TODO: Do we need a modifier here for subsurface scattering?

    if (NdotL <= 0.0)
	{
		// No light is being cast.
        diffuseContribution = float3(0.0f, 0.0f, 0.0f);
        specularContribution = float3(0.0f, 0.0f, 0.0f);
		return NdotL;
	}

    float3 lightColor = 
                CalculateFalloff(light, float4(lightWorldPosition, 1.0)) *
				CalculateProjection(light, float4(lightWorldPosition, 1.0));

    if (length(lightColor) <= 0.0)
    {
        // No light is reflected.
        diffuseContribution = float3(0.0f, 0.0f, 0.0f);
        specularContribution = float3(0.0f, 0.0f, 0.0f);
        return 0.0f;
    }

    GeneralBRDF(
        lightColor,
        light.diffuseColor.rgb,
        light.specularColor.rgb,
        NdotL,
        surfaceNormal,
        distribution,
        baseReflectivity, // Base reflectivity, we need to eventually adjust this per material.
        roughness, // Roughness, we need to eventually adjust this per material.
        viewVector, // View vector
        normalize(lightVector + viewVector), // Half vector
        diffuseContribution,
        specularContribution
    );

    return NdotL;
}

SamplerState    pointSampler : register(s0, space0); // Replace this with Sampler that will grab the exact point information from the shader.
SamplerState    projectionSampler : register(s1, space0);