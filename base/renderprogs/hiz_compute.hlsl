// Generates a Hierarchical Z Buffer
#include "global.inc"

#define WIDTH_HEIGHT_EVEN 0 // Both width and height are even
#define WIDTH_ODD_HEIGHT_EVEN 1 // Only the width is odd
#define WIDTH_EVEN_HEIGHT_ODD 2 // Only the height is odd
#define WIDTH_HEIGHT_ODD 3 // Bot the with and height are odd

struct GenerateMipInput {
    uint srcMipLevel;
    uint numMipLevels;
    uint srcDimension; // Width and Height are even or odd. Defined above
    uint pad0;
};
ConstantBuffer<GenerateMipInput> mipConstants : register(b2, space0);

RWTexture2D<float> srcMip : register(u0);

// Write out to the 4 mip levels
RWTexture2D<float> outMip[3] : register(u1);

groupshared float depthShared[8][8]; // Used to sample the speth into an easily sharable buffer.

[numthreads(8, 8, 1)] // 8x8x1 to match the depthShared component
void main(uint2 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
	// Example modified from here https://www.3dgep.com/learning-directx-12-4/#Generate_Mipmaps_Compute_Shader
    float srcDepth = 0;
	
    uint width, height;
    srcMip.GetDimensions(width, height);
    uint2 frag = min(DTid.xy, uint2(width - 1, height - 1));
    
    switch (mipConstants.srcDimension)
    {
        case WIDTH_HEIGHT_EVEN:
            {
                srcDepth = srcMip[frag];
            }
            break;
        
        case WIDTH_ODD_HEIGHT_EVEN:
            {
                uint2 offset = uint2(min(frag.x + 1, width - 1), frag.y);
            
                srcDepth = min(srcMip[frag], srcMip[offset]);
            }
            break;
        
        case WIDTH_EVEN_HEIGHT_ODD:
            {
                uint2 offset = uint2(frag.x, min(frag.y + 1, height - 1));
            
                srcDepth = min(srcMip[frag], srcMip[offset]);
            }
            break;
        
        case WIDTH_HEIGHT_ODD:
            {
                uint2 offset = min(uint2(frag.x + 1, frag.y + 1), uint2(width - 1, height - 1));
            
                srcDepth = min(srcMip[frag], srcMip[offset]);
                srcDepth = min(srcDepth, srcMip[uint2(frag.x, offset.y)]);
                srcDepth = min(srcDepth, srcMip[uint2(offset.x, frag.y)]);
            }
            break;
    }
    
    depthShared[GTid.x][GTid.y] = srcDepth;
      
    const uint maxCount = max(mipConstants.numMipLevels - 1, 3);
    uint checkId = 0x01u;
    uint checkIndex = 0x01u;
    
    if(maxCount == 0)
    {
        return;
    }
    
    GroupMemoryBarrierWithGroupSync(); // Sync between all groups until we are ready.
    
    // Checks if x and y are even as they would load with 0x001001
    if ((GTid.x & checkId) == 0 && (GTid.y & checkId) == 0)
    {
        float depth2 = depthShared[GTid.x][GTid.y + checkIndex];
        float depth3 = depthShared[GTid.x + checkIndex][GTid.y];
        float depth4 = depthShared[GTid.x + checkIndex][GTid.y + checkIndex];
        srcDepth = min(srcDepth, min(depth2, min(depth3, depth4)));
            
        outMip[0][DTid.xy / (checkIndex << 1)] = srcDepth;
        depthShared[GTid.x][GTid.y] = srcDepth;
    }
        
    checkId |= checkIndex;
    
    if (maxCount == 1)
    {
        return;
    }
    
    GroupMemoryBarrierWithGroupSync(); // Sync between all groups until we are ready.
    checkIndex = 0x01u << 1;
        
    // Checks if x and y are even as they would load with 0x011011
    if ((GTid.x & checkId) == 0 && (GTid.y & checkId) == 0)
    {
        float depth2 = depthShared[GTid.x][GTid.y + checkIndex];
        float depth3 = depthShared[GTid.x + checkIndex][GTid.y];
        float depth4 = depthShared[GTid.x + checkIndex][GTid.y + checkIndex];
        srcDepth = min(srcDepth, min(depth2, min(depth3, depth4)));
            
        outMip[1][DTid.xy / (checkIndex << 1)] = srcDepth;
        depthShared[GTid.x][GTid.y] = srcDepth;
    }
        
    checkId |= checkIndex;
    
    if (maxCount == 2)
    {
        return;
    }
    
    GroupMemoryBarrierWithGroupSync(); // Sync between all groups until we are ready.
    checkIndex = 0x01u << 2;
        
    // Checks if x and y are even as they would load with 0x111111
    if ((GTid.x & checkId) == 0 && (GTid.y & checkId) == 0)
    {
        float depth2 = depthShared[GTid.x][GTid.y + checkIndex];
        float depth3 = depthShared[GTid.x + checkIndex][GTid.y];
        float depth4 = depthShared[GTid.x + checkIndex][GTid.y + checkIndex];
        srcDepth = min(srcDepth, min(depth2, min(depth3, depth4)));
            
        outMip[2][DTid.xy / (checkIndex << 1)] = srcDepth;
        depthShared[GTid.x][GTid.y] = srcDepth;
    }
        
    checkId |= checkIndex;
}