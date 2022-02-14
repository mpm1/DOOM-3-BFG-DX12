// Ray payload. Contains:
// Diffuse color
// Emmissive color
// Basic infor for a hit.
struct HitInfo
{
	float	light; // Used to calculate the ammount of light. 0 - In Shadow, 1 - In light
	float3	normal; // Used to calculate any ray bounces.
};

// Attributes output by the raytracing when hitting a surface:
struct Attributes
{
  float4	location; // Location of the hit.
};