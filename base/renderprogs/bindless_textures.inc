#define BINDLESS_TEXTURE_COUNT	4096
#define BINDLESS_SAMPLER_COUNT  512

struct BindlessSamplerIndex
{
	int frameIndex;
	uint samplerIndex;
	uint pad1;
	uint pad2;
};

struct BindlessSamplers
{
	BindlessSamplerIndex samplers[BINDLESS_TEXTURE_COUNT];
};

uniform ConstantBuffer<BindlessSamplers> samplerIndecies : register(b0, space1);

uniform SamplerState samplers[BINDLESS_SAMPLER_COUNT] : register(s0, space1);

uniform Texture2D<float>	bindlessTextures1f[BINDLESS_TEXTURE_COUNT]	: register(t0, space1);
uniform Texture2D<float4>	bindlessTextures4f[BINDLESS_TEXTURE_COUNT]	: register(t0, space2);

Texture2D<float> GetBindlessTexture1f(uint32_t textureIndex)
{
	Texture2D<float> texture = bindlessTextures1f[textureIndex];

	return texture;
}

Texture2D<float4> GetBindlessTexture4f(uint32_t textureIndex)
{
	Texture2D<float4> texture = bindlessTextures4f[textureIndex];

	return texture;
}

float4 SampleBindlessTexture4f(uint32_t textureIndex, float2 uv)
{
	uint samplerIndex = samplerIndecies.samplers[textureIndex].samplerIndex;
	
	return GetBindlessTexture4f(textureIndex).Sample(samplers[samplerIndex], uv);
}

float4 SampleBindlessTexture4fLevel(uint32_t textureIndex, float2 uv, uint level)
{
	uint samplerIndex = samplerIndecies.samplers[textureIndex].samplerIndex;
	
	return GetBindlessTexture4f(textureIndex).SampleLevel(samplers[samplerIndex], uv, level);
}