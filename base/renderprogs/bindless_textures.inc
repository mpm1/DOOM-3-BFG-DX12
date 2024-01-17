#define BINDLESS_TEXTURE_COUNT	1024

uniform Texture2D<float4>	bindlessTextures4f[BINDLESS_TEXTURE_COUNT]	: register(t3, space0);

Texture2D<float4> GetBindlessTexture4f(uint32_t textureIndex)
{
	Texture2D<float4> texture = bindlessTextures4f[textureIndex];

	return texture;
}