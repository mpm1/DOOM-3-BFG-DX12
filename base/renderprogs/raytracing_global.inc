// Ray payload. Contains:
// Diffuse color
// Emmissive color
// Basic infor for a hit.
struct HitInfo
{
	float3 diffuse;
	float3 indirect;
	float3 specular;
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

inline RayDesc GenerateCameraRay(uint2 index, in SceneConstants constants)
{
    float2 xy = index + 0.5f; // Center of the pixel
    float2 screenPos = (xy / DispatchRaysDimensions().xy) * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a world positon.
    float4 world = GetInverseProjection(float4(screenPos, 0, 1), constants);
    world /= world.w;

    RayDesc ray;
    ray.Origin = GetInverseView(float4(0, 0, 0, 1), constants).xyz;
    ray.Direction = normalize(GetInverseView(world, constants).xyz - ray.Origin);

    return ray;
}