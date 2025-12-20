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

Texture2D<float4> srcMip : register(t0);

// Write out to the 4 mip levels
RWTexture2D<float> outMip[4] : register(u1);

groupshared float depthShared[64]; // Used to sample the speth into an easily sharable buffer.

[numthreads(8, 8, 1)] // 8x8x1 to match the depthShared component
void main(uint GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
	// Example modified from here https://www.3dgep.com/learning-directx-12-4/#Generate_Mipmaps_Compute_Shader
    float srcDepth = 0;
	
    switch (mipConstants.srcDimension)
    {
        case WIDTH_HEIGHT_EVEN:
            {
                float2 uv = mipConstants.texalSize * (DTid.xy + 0.5);
                srcDepth = srcMip.SampleLevel(pointSampler, uv, mipConstants.srcMipLevel);
            }
            break;
        
        case WIDTH_ODD_HEIGHT_EVEN:
            {
                float2 uv = mipConstants.texalSize * (DTid.xy + float2(0.25, 0.5));
                float2 offset = mipConstants.texalSize * float2(0.5, 0.0);
            
                srcDepth = max(srcMip.SampleLevel(pointSampler, uv, mipConstants.srcMipLevel), srcMip.SampleLevel(pointSampler, uv + offset, mipConstants.srcMipLevel));
            }
            break;
        
        case WIDTH_EVEN_HEIGHT_ODD:
            {
                float2 uv = mipConstants.texalSize * (DTid.xy + float2(0.5, 0.25));
                float2 offset = mipConstants.texalSize * float2(0.0, 0.5);
            
                srcDepth = max(srcMip.SampleLevel(pointSampler, uv, mipConstants.srcMipLevel), srcMip.SampleLevel(pointSampler, uv + offset, mipConstants.srcMipLevel));
            }
            break;
        
        case WIDTH_HEIGHT_ODD:
            {
                
                float2 uv = mipConstants.texalSize * (DTid.xy + 0.25);
                float2 offset = mipConstants.texalSize * 0.5;
            
                srcDepth = max(srcMip.SampleLevel(pointSampler, uv, mipConstants.srcMipLevel), srcMip.SampleLevel(pointSampler, uv + float2(offset.x, 0), mipConstants.srcMipLevel));
                srcDepth = max(srcDepth, srcMip.SampleLevel(pointSampler, uv + float2(0, offset.y), mipConstants.srcMipLevel));
                srcDepth = max(srcDepth, srcMip.SampleLevel(pointSampler, uv + offset, mipConstants.srcMipLevel));
            }
            break;
    }
    
    outMip[0][DTid.xy] = srcDepth;
    
    if (mipConstants.numMipLevels <= 1)
        return;
    
    depthShared[GTid] = srcDepth;
        
    const uint maxCount = min(mipConstants.numMipLevels - mipConstants.srcMipLevel, 4);
    uint div = 1;
    
    for (uint i = 0; i < maxCount; ++i)
    {
        GroupMemoryBarrierWithGroupSync(); // Sync between all groups until we are ready.
        div = div << 1;
        
        // Checks if x and y are even as they would load with 0x001001
        if ((GTid & 0x9) == 0)
        {
            float depth2 = depthShared[GTid + 0x01];
            float depth3 = depthShared[GTid + 0x08];
            float depth4 = depthShared[GTid + 0x09];
            srcDepth = max(srcDepth, max(depth2, max(depth3, depth4)));
            
            outMip[i][DTid.xy / div] = srcDepth;
            depthShared[GTid] = srcDepth;
        }
    }
}