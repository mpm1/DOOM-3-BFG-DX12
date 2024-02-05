#ifndef __DX12_TEXTURE_MANAGER__
#define __DX12_TEXTURE_MANAGER__

#include "./dx12_resource.h"

namespace DX12Rendering
{
	enum eGlobalTexture
	{
		DEPTH_TEXTURE,
		VIEW_DEPTH,
		ALBEDO,
		SPECULAR_COLOR,
		WORLD_NORMALS,
		WORLD_FLAT_NORMALS,
		RAYTRACED_SHADOWMAP,

		TEXTURE_COUNT
	};

	struct TextureBuffer : public Resource
	{
	public:
		friend class TextureManager;

		D3D12_SHADER_RESOURCE_VIEW_DESC textureView;

		TextureBuffer(const LPCWSTR name) : Resource(name),
			textureView{}
		{
		}

		bool Build(D3D12_RESOURCE_DESC& textureDesc, D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc, D3D12_RESOURCE_STATES resourceState);

		const bool IsReady() { return Exists(); }

		const CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle() const { return m_gpuHandle; }
		void SetGPUDescriptorHandle(CD3DX12_GPU_DESCRIPTOR_HANDLE handle) { m_gpuHandle = handle; }
	private:
		D3D12_RESOURCE_DESC m_textureDesc;
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
	};

	class TextureManager {
	public:
		TextureManager();
		~TextureManager();

		void Initialize(uint screenWidth, uint screenHeight);
		void Clear();

		// State Control
		bool SetTextureCopyState(TextureBuffer* buffer) const;
		bool SetTexturePixelShaderState(TextureBuffer* buffer) const;
		bool SetTextureState(TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList) const;

		// Data management
		void StartTextureWrite(TextureBuffer* buffer);
		void EndTextureWrite(TextureBuffer* buffer);

		TextureBuffer* AllocTextureBuffer(const idStr* name, D3D12_RESOURCE_DESC& textureDesc, const UINT shaderComponentMapping, D3D12_RESOURCE_STATES resourceState);

		TextureBuffer* GetTextureBuffer(uint64 textureHandle); //TODO: Move everything to a reference to create bindless textures.
		void FreeTextureBuffer(TextureBuffer* buffer);
		void SetTextureContent(TextureBuffer* buffer, const UINT resourceIndex, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image);

		TextureBuffer* GetGlobalTexture(eGlobalTexture textureId);

		// Stores images in temporary data. This is reset on EndTextureWrite.
		byte* CreateTemporaryImageStorage(const UINT imageSize);

	private:
		ScratchBuffer m_textureUploadHeap;
		// TODO: Create bindless textures.
		std::vector<DX12Rendering::TextureBuffer*> m_textures; // Stores the active texture information in the scene.

		std::vector<std::unique_ptr<byte[]>> m_tempImages;
	};
}
#endif