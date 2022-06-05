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