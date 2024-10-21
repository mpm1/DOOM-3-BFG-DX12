#define BINDLESS_TEXTURE_COUNT	4096

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