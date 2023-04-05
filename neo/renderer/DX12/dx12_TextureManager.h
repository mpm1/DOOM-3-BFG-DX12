#ifndef __DX12_TEXTURE_MANAGER__
#define __DX12_TEXTURE_MANAGER__

#include "./dx12_resource.h"

namespace DX12Rendering
{//Mark start here. This was renamed from DX12TextureBuffer. We need to move the copy fence and the texture upload heap over to the manager.
	struct TextureBuffer : Resource
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC textureView;
		D3D12_RESOURCE_STATES m_lastTransitionState;

		TextureBuffer(const LPCWSTR name) : Resource(name),
			textureView{},
			m_lastTransitionState(D3D12_RESOURCE_STATE_COMMON)
		{
		}

		bool Build(D3D12_RESOURCE_DESC& textureDesc, D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc);

		const bool IsReady() { return Exists(); }
	};

	class TextureManager {
	public:
		TextureManager();
		~TextureManager();

		void Clear();

		// State Control
		bool SetTextureCopyState(TextureBuffer* buffer, const UINT mipLevel);
		bool SetTexturePixelShaderState(TextureBuffer* buffer, const UINT mipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		bool SetTextureState(TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList, const UINT mipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

		// Data management
		void StartTextureWrite(TextureBuffer* buffer);
		void EndTextureWrite(TextureBuffer* buffer);

		TextureBuffer* AllocTextureBuffer(const idStr* name, D3D12_RESOURCE_DESC& textureDesc);
		TextureBuffer* GetTextureBuffer(uint64 textureHandle); //TODO: Move everything to a reference to create bindless textures.
		void FreeTextureBuffer(TextureBuffer* buffer);
		void SetTextureContent(TextureBuffer* buffer, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image);

	private:
		ScratchBuffer m_textureUploadHeap;
		std::vector<DX12Rendering::TextureBuffer> m_textures; // Stores the active texture information in the scene.
	};
}
#endif