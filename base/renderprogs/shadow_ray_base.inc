#include "raytracing_global.inc"

// Raytracing output to texture, accessed as UAV
RWTexture2D<float4> diffuseResult :register(u0);
RWTexture2D<float4> specularResult :register(u1);

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH :register(t0);

//TODO: turn this into the base for all lights. That way we can make separate shaders for Point, Ambient, and Fog shaders.