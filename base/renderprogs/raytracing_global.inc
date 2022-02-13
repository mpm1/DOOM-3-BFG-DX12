// Ray payload. Contains:
// Diffuse color
// Emmissive color
// Light color - The current color of light held by the ray.
// TODO: Add more
struct HitInfo
{
	float4 diffuseColor;
	float3 emissiveColor;
	float3 lightColor;
};

// Attributes output by the raytracing when hitting a surface:
// barycentric coordinates
// UV Texture Coordinates
struct Attributes
{
  float2 bary;
  float2 uv;
};