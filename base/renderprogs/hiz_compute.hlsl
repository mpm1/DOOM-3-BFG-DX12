// Calculates the BLAS animations through sekleton manipulation.
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
	
    float2 texalSize; // 1.0 / Mip0 
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
	
    switch (mipConstants.srcDimension)
    {
        case WIDTH_HEIGHT_EVEN:
            {
                srcDepth = srcMip[DTid.xy];
            }
            break;
        
        case WIDTH_ODD_HEIGHT_EVEN:
            {
                uint2 offset = int2(1, 0);
            
                srcDepth = max(srcMip[DTid.xy], srcMip[DTid.xy + offset]);
            }
            break;
        
        case WIDTH_EVEN_HEIGHT_ODD:
            {
                uint2 offset = int2(0, 1);
            
                srcDepth = max(srcMip[DTid.xy], srcMip[DTid.xy + offset]);
            }
            break;
        
        case WIDTH_HEIGHT_ODD:
            {
                uint2 offset = int2(1, 1);
            
                srcDepth = max(srcMip[DTid.xy], srcMip[DTid.xy + offset]);
                srcDepth = max(srcDepth, srcMip[DTid.xy + uint2(offset.x, 0)]);
                srcDepth = max(srcDepth, srcMip[DTid.xy + uint2(0, offset.y)]);
            }
            break;
    }
    
    outMip[0][DTid.xy] = srcDepth;
    
    if (mipConstants.numMipLevels <= 1)
        return;
    
    depthShared[GTid.x][GTid.y] = srcDepth;
        
    const uint maxCount = min(mipConstants.numMipLevels - mipConstants.srcMipLevel, 3);
    uint div = 1;
    uint checkId = 0x01;
    
    for (uint i = 0; i < maxCount; ++i)
    {
        GroupMemoryBarrierWithGroupSync(); // Sync between all groups until we are ready.
        div = div << 1;
        
        // Checks if x and y are even as they would load with 0x001001
        if ((GTid.x & checkId) == 0 && (GTid.y & checkId) == 0)
        {
            float depth2 = depthShared[GTid.x][GTid.y + (0x01u << i)];
            float depth3 = depthShared[GTid.x + (0x01u << i)][GTid.y];
            float depth4 = depthShared[GTid.x + (0x01u << i)][GTid.y + (0x01u << i)];
            srcDepth = max(srcDepth, max(depth2, max(depth3, depth4)));
            
            outMip[i][DTid.xy / div] = srcDepth;
            depthShared[GTid.x][GTid.y] = srcDepth;
        }
        
        checkId |= (0x01u << i);
    }
}