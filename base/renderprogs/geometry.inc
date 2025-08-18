struct BLASVertex { // TODO: modify this to take our entire structure properly
	float3 position; //DXGI_FORMAT_R32G32B32_FLOAT
	uint texcoord; // DXGI_FORMAT_R16G16_FLOAT
	uint normal; // DXGI_FORMAT_R8G8B8A8_UNORM
	uint tangent; // DXGI_FORMAT_R8G8B8A8_UNORM
	uint color; // DXGI_FORMAT_R8G8B8A8_UNORM
	uint color2; // DXGI_FORMAT_R8G8B8A8_UNORM
};

struct BLASIndex
{
    uint index; // Use 32-bit uint, since HLSL does not natively support 16-bit integers in buffers
};

// Matches RaytracingGeometryArgument in dx12_AccerlationStructure.h
struct RaytracingGeometryArgument
{
    uint64_t  meshIndex;
    uint64_t  vertexHandle;
    uint64_t  indexHandle;
    uint64_t  jointsHandle;

    uint    vertCounts;
    uint    vertOffset;
    uint    indexCounts;
    uint    pad0;
};

uint UnpackBLASIndex(uint index, StructuredBuffer<BLASIndex> indices)
{
    // Each index is 16-bits, so we need to unpack from a 32-bit integer.
    uint packed = indices[index >> 1].index;
    uint actualIndex = (index % 2 == 0) ? (packed >> 16) : (packed & 0xFFFF);
    return actualIndex;
}