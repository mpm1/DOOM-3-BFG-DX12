#ifndef __DX12_TEXTURE_MANAGER__
#define __DX12_TEXTURE_MANAGER__

#include "./dx12_resource.h"
#include <renderer/DX12/dx12_CommandList.h>
#include "./dx12_HeapDescriptorManager.h"

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

		RAYTRACED_DIFFUSE,
		RAYTRACED_SPECULAR,

		TEXTURE_COUNT
	};

	struct TextureBuffer : public Resource
	{
	public:
		friend class TextureManager;

		D3D12_SHADER_RESOURCE_VIEW_DESC textureView;

		TextureBuffer(const LPCWSTR name) : Resource(name),
			textureView{},
			m_lastFenceValue(DX12Rendering::Commands::COPY, 0)
		{
		}

		bool Build(D3D12_RESOURCE_DESC& textureDesc, D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc, D3D12_RESOURCE_STATES resourceState);

		const bool IsReady() { return Exists(); }

		const CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle() const { return m_gpuHandle; }
		void SetGPUDescriptorHandle(CD3DX12_GPU_DESCRIPTOR_HANDLE handle) { m_gpuHandle = handle; }

		const UINT GetTextureIndex() const { return m_heapIndex; }

		const Commands::FenceValue& GetLastFenceValue() { return m_lastFenceValue; }
		const Commands::FenceValue& SetLastFenceValue(const Commands::FenceValue& fence)
		{
			m_lastFenceValue.commandList = fence.commandList;
			m_lastFenceValue.value = fence.value;

			return m_lastFenceValue;
		}

	private:
		D3D12_RESOURCE_DESC m_textureDesc;
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
		UINT m_heapIndex;

		Commands::FenceValue m_lastFenceValue;
	};

	class TextureManager {
	public:
		static const UINT BINDLESS_TEXTURE_COUNT = 4096;
		static const UINT TEXTURE_SPACE_COUNT = 2;

		TextureManager();
		~TextureManager();

		void Initialize(uint screenWidth, uint screenHeight);
		void Clear();

		// State Control
		bool SetTextureCopyState(TextureBuffer* buffer, DX12Rendering::Commands::CommandList* commandList) const;
		bool SetTexturePixelShaderState(TextureBuffer* buffer, DX12Rendering::Commands::CommandList* commandList) const;
		bool SetTextureState(TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList) const;

		// Data management
		void StartTextureWrite(TextureBuffer* buffer);
		const DX12Rendering::Commands::FenceValue EndTextureWrite(TextureBuffer* buffer);

		/// <summary>
		/// Generates a texture to be stored in video memory
		/// </summary>
		/// <param name="name">Name of the texture.</param>
		/// <param name="textureDesc"></param>
		/// <param name="shaderComponentMapping">Any special mapping we need to define our RGB components</param>
		/// <param name="resourceState">The default state for the texture.</param>
		/// <param name="index">Index to store the texture in the system. A value less than 1 will append the texture to the end.</param>
		TextureBuffer* AllocTextureBuffer(const idStr* name, D3D12_RESOURCE_DESC& textureDesc, const UINT shaderComponentMapping, D3D12_RESOURCE_STATES resourceState, int index = -1);

		TextureBuffer* GetTextureBuffer(uint64 textureHandle); //TODO: Move everything to a reference to create bindless textures.
		void FreeTextureBuffer(TextureBuffer* buffer);
		void SetTextureContent(TextureBuffer* buffer, const UINT resourceIndex, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image);

		TextureBuffer* GetGlobalTexture(eGlobalTexture textureId);

		// Stores images in temporary data. This is reset on EndTextureWrite.
		byte* CreateTemporaryImageStorage(const UINT imageSize);

		// PipelineFunctions
		const D3D12_DESCRIPTOR_RANGE1* GetDescriptorRanges() { return m_descriptorRanges; }
	private:
		ScratchBuffer m_textureUploadHeap;
		
		CD3DX12_DESCRIPTOR_RANGE1 m_descriptorRanges[TEXTURE_SPACE_COUNT];

		std::vector<DX12Rendering::TextureBuffer*> m_textures; // Stores the active texture information in the scene.

		std::vector<std::unique_ptr<byte[]>> m_tempImages;

		void SetTextureToDefault(CD3DX12_CPU_DESCRIPTOR_HANDLE textureHandle);
	};

	TextureManager* GetTextureManager();
	void DestroyTextureManager();
}
#endif