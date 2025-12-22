#ifndef __DX12_TEXTURE_MANAGER__
#define __DX12_TEXTURE_MANAGER__

#include "./dx12_resource.h"
#include <renderer/DX12/dx12_CommandList.h>
#include "./dx12_HeapDescriptorManager.h"

namespace DX12Rendering
{
	typedef GUID SamplerKey;

	enum eGlobalTexture
	{
		DEPTH_TEXTURE,
		SPECULAR_COLOR,
		MATERIAL_PROPERTIES, // r = Roughness, g = Metallic, b = occlusion? (not implemented)
		WORLD_FLAT_NORMALS,
		WORLD_FLAT_TANGENT,
		RAYTRACED_SHADOWMAP,

		RAYTRACED_DIFFUSE,
		RAYTRACED_SPECULAR,
		RAYTRACED_GLI,

		LAST_FRAME_UNTOUCHED,

		TEXTURE_COUNT
	};

	struct BindlessSamplerIndex
	{
		int frame; // The frame when we recorded this value. That way we can keep this up to date
		UINT samplerMapIndex; // Where on the sampler heap we find this sampler
		UINT pad1;
		UINT pad2;
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

		bool Build(D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc, D3D12_RESOURCE_STATES resourceState);
		bool AttachToResource(const DX12Rendering::Resource* resource, D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_RESOURCE_VIEW_DESC& srcDes);

		const bool IsReady() { return Exists(); }

		const D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle() const { return m_gpuHandle; }
		void SetGPUDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { m_gpuHandle = handle; }

		const UINT GetTextureIndex() const { return m_heapIndex; }

		const Commands::FenceValue& GetLastFenceValue() { return m_lastFenceValue; }
		const Commands::FenceValue& SetLastFenceValue(const Commands::FenceValue& fence)
		{
			m_lastFenceValue.commandList = fence.commandList;
			m_lastFenceValue.value = fence.value;

			return m_lastFenceValue;
		}

		const D3D12_SAMPLER_DESC* GetSamplerDescription() const { return &m_samplerDesc; };

	private:
		D3D12_RESOURCE_DESC m_textureDesc;
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
		UINT m_heapIndex;

		D3D12_SAMPLER_DESC m_samplerDesc;

		Commands::FenceValue m_lastFenceValue;
	};

	class TextureManager {
	public:
		static const UINT BINDLESS_TEXTURE_COUNT = TEXTURE_ENTRIES_HEAP_SIZE;
		static const UINT BINDLESS_SAMPLER_COUNT = SAMPLERS_ENTRIES_HEAP_SIZE;
		static const UINT TEXTURE_SPACE_COUNT = 2;
		static const UINT CONSTANT_DESCRIPTOR_COUNT = 1;
		static const UINT SAMPLER_DESCRIPTOR_COUNT = 1;
		static const UINT DESCRIPTOR_COUNT = TEXTURE_SPACE_COUNT + CONSTANT_DESCRIPTOR_COUNT + SAMPLER_DESCRIPTOR_COUNT;

		TextureManager();
		~TextureManager();

		void BeginFrame(const int frameIndex);

		void Initialize(uint screenWidth, uint screenHeight);
		void ResizeGlobalTextures(uint screenWidth, uint screenHeight);
		void Clear();

		// State Control
		bool SetTextureCopyState(TextureBuffer* buffer, DX12Rendering::Commands::CommandList* commandList) const;
		bool SetTexturePixelShaderState(TextureBuffer* buffer, DX12Rendering::Commands::CommandList* commandList) const;
		bool SetTextureState(TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList) const;

		// Data management
		void StartTextureWrite(TextureBuffer* buffer);
		bool EndTextureWrite(TextureBuffer* buffer);

		/// <summary>
		/// Generates a texture to be stored in video memory
		/// </summary>
		/// <param name="name">Name of the texture.</param>
		/// <param name="textureDesc"></param>
		/// <param name="shaderComponentMapping">Any special mapping we need to define our RGB components</param>
		/// <param name="resourceState">The default state for the texture.</param>
		/// <param name="index">Index to store the texture in the system. A value less than 1 will append the texture to the end.</param>
		TextureBuffer* AllocTextureBuffer(const idStr* name, D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, const UINT shaderComponentMapping, D3D12_RESOURCE_STATES resourceState, int index = -1);

		TextureBuffer* GenerateFromExistingResource(const DX12Rendering::Resource* resource, D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

		void FreeTextureBuffer(TextureBuffer* buffer);
		void SetTextureContent(TextureBuffer* buffer, const UINT resourceIndex, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image);

		TextureBuffer* GetGlobalTexture(eGlobalTexture textureId);

		// Stores images in temporary data. This is reset on EndTextureWrite.
		byte* CreateTemporaryImageStorage(const UINT imageSize);
		
		// Stores the information for a sampler into the sampler heap. An index is then recorded to make sure we don't create multiple samplers for the same item.
		void RecordSamplerForFrame(const int frameIndex, const UINT textureIndex);

		// Stores teh sampler map for the frame into GPU memory.
		void StoreFrameSamplers();

		// PipelineFunctions
		const D3D12_DESCRIPTOR_RANGE1* GetDescriptorRanges() { return m_descriptorRanges; }

		const bool IsInitialized() { return m_isInitialized; }

	private:
		ScratchBuffer m_textureUploadHeap;

		bool m_isInitialized;
		
		D3D12_DESCRIPTOR_RANGE1 m_descriptorRanges[DESCRIPTOR_COUNT];

		std::vector<DX12Rendering::TextureBuffer*> m_textures; // Stores the active texture information in the scene.
		DX12Rendering::BindlessSamplerIndex m_samplers[BINDLESS_TEXTURE_COUNT]; // Stores the active sampler index for each texture. The texture vertex must match the index of each entry.
		SamplerKey m_samplerHash[BINDLESS_TEXTURE_COUNT];

		UINT m_nextSamplerLocation;

		std::vector<std::unique_ptr<byte[]>> m_tempImages;

		void SetTextureToDefault(UINT textureIndex);

		int StoreTextureEntry(TextureBuffer* buffer, int index);

		TextureBuffer* GetTextureBuffer(const UINT index) { return m_textures[index]; }

		const SamplerKey GetSamplerKey(D3D12_SAMPLER_DESC& samplerDesc) noexcept
		{
			SamplerKey result{};

			MurmurHash3_x64_128(&samplerDesc, sizeof(D3D12_SAMPLER_DESC), 111, &result);

			return result;
		}
	};

	TextureManager* GetTextureManager();
	void DestroyTextureManager();
}
#endif