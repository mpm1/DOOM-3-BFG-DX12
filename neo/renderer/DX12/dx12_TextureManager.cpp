#pragma hdrstop


#include "../tr_local.h"

#include "./dx12_TextureManager.h"

namespace DX12Rendering
{
	bool TextureBuffer::AttachToResource(const DX12Rendering::Resource* resource, D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_RESOURCE_VIEW_DESC& srcDesc)
	{
		if (resource->state != Ready)
		{
			return false;
		}

		m_textureDesc = textureDesc;
		m_samplerDesc = samplerDesc;
		textureView = srcDesc;

		this->resource = resource->resource;
		this->state = Ready;

		return true;
	}

	bool TextureBuffer::Build(D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc, D3D12_RESOURCE_STATES resourceState)
	{
		Release();

		m_textureDesc = textureDesc;
		m_samplerDesc = samplerDesc;

		Allocate(textureDesc, resourceState, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT));

		textureView = srcDesc;

		return true;
	}

	TextureManager::TextureManager() :
		m_isInitialized(false),
		m_nextSamplerLocation(0),
		m_textureUploadHeap(3840*2160*4*16, D3D12_SMALL_MSAA_RESOURCE_PLACEMENT_ALIGNMENT, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, L"Texture Buffer Upload Resource Heap")
	{
		m_textures.reserve(BINDLESS_TEXTURE_COUNT);
		std::memset(m_samplers, 0, sizeof(m_samplers));

		int descriptorIndex = 0;

		for (uint space = 0; space < TEXTURE_SPACE_COUNT; ++space)
		{
			m_descriptorRanges[descriptorIndex] = CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV /* Texture Array */,
				BINDLESS_TEXTURE_COUNT, /* Total number of possible textures */
				0 /*t0*/,
				space + 1, /* space1. We will define different spaces if we want to use more than texture 2D, but use the same data. */
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				0 // Starting index of the heap
			);

			++descriptorIndex;
		}

		m_descriptorRanges[descriptorIndex] = CD3DX12_DESCRIPTOR_RANGE1(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV, /* Constant Buffer */
			CONSTANT_DESCRIPTOR_COUNT, /* 1 Constant Buffer */
			0, /*b0*/
			1, /* space1 */
			D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
			0 // Starting index of the heap
		);
		++descriptorIndex;

		m_descriptorRanges[descriptorIndex] = CD3DX12_DESCRIPTOR_RANGE1(
			D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, /* Sampler Array */
			BINDLESS_SAMPLER_COUNT, /* 512 Samplers */
			0, /*s0*/
			1, /* space1 */
			D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
			0 // Starting index of the heap
		);
	}

	TextureManager::~TextureManager() {
		Clear();
	}

	TextureBuffer* GetGlobalTexture(eGlobalTexture textureId);

	const char* GetGlobalTextureDetails(eGlobalTexture textureId, D3D12_RESOURCE_DESC& textureDesc)
	{
		// Obtain the name and format of the desired write to texture.
		char* name;

		switch (textureId)
		{
		case eGlobalTexture::DEPTH_TEXTURE:
			name = "depth_texture";
			textureDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			break;

		case eGlobalTexture::POSITION:
			name = "position";
			textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			break;

		case eGlobalTexture::WORLD_NORMALS:
			name = "world_normals";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::ALBEDO:
			name = "albedo";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::SPECULAR_COLOR:
			name = "specular_color";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::MATERIAL_PROPERTIES:
			name = "material_properties";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::REFLECTION_VECTOR:
			name = "reflection_vector";
			textureDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
			break;

		case eGlobalTexture::REFLECTIONS:
			name = "reflections";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::LAST_FRAME_UNTOUCHED:
			name = "lastRenderedFrameNoFX";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::HIZ_DEPTH:
			name = "HeirarchicalDepth";
			textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
			textureDesc.MipLevels = 5;
			textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			break;

		case eGlobalTexture::RAYTRACED_DIFFUSE:
			name = "raytraced_diffuse";
			textureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
			break;

		case eGlobalTexture::RAYTRACED_SPECULAR:
			name = "raytraced_specular";
			textureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
			break;

		case eGlobalTexture::RAYTRACED_GLI:
			name = "raytraced_global_illumination";
			textureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
			break;

		case eGlobalTexture::WORLD_FLAT_NORMALS:
			name = "world_flat_normals";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::WORLD_FLAT_TANGENT:
			name = "world_flat_tangent";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;

		case eGlobalTexture::RAYTRACED_SHADOWMAP:
			name = "raytraced_shadowmap";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
			break;

		default:
			name = "default_global_texture";
			textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		}

		return name;
	}

	D3D12_SAMPLER_DESC CreateGlobalSamplerDesc()
	{
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MaxLOD = FLT_MAX;
		samplerDesc.BorderColor[3] = 0.0f; // Set alpha to 0.0
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

		return samplerDesc;
	}

	void TextureManager::BeginFrame(const int frameIndex)
	{
		m_nextSamplerLocation = 0;

		// Record the global texutre samplers
		for (UINT i = 0; i < eGlobalTexture::TEXTURE_COUNT; ++i)
		{
			RecordSamplerForFrame(frameIndex, i);
		}
	}

	void TextureManager::Initialize(uint screenWidth, uint screenHeight) {	
		// Create the basic entries for the global textures
		for (int i = 0; i < eGlobalTexture::TEXTURE_COUNT; ++i)
		{
			TextureBuffer* globalTexture = GetGlobalTexture(static_cast<eGlobalTexture>(i));
			if (globalTexture != nullptr)
			{
				delete(globalTexture);
			}

			D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_UNKNOWN, screenWidth, screenHeight, 1, 1);
			UINT shaderComponentAlignment = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			const char* name = GetGlobalTextureDetails(static_cast<eGlobalTexture>(i), textureDesc);

			D3D12_SAMPLER_DESC samplerDesc = CreateGlobalSamplerDesc();

			globalTexture = AllocTextureBuffer(
				&idStr(name), textureDesc, samplerDesc,
				shaderComponentAlignment, D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON,
				m_textures.size() <= i ? -1 : i);
			
		}

		// Fill in all textures
		for (int i = eGlobalTexture::TEXTURE_COUNT; i < BINDLESS_TEXTURE_COUNT; ++i)
		{
			SetTextureToDefault(i);
		}

		// Fill in all samplers
		for (int i = 0; i < BINDLESS_SAMPLER_COUNT; ++i)
		{
			HeapDescriptorManager* heapManager = GetDescriptorManager();
			D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle = heapManager->GetCPUDescriptorHandle(eHeapDescriptorSamplerEntries, i);
			Device::GetDevice()->CreateSampler(&GetTextureBuffer(0)->m_samplerDesc, samplerHandle);
		}

		m_isInitialized = true;
	}

	void TextureManager::ResizeGlobalTextures(uint screenWidth, uint screenHeight)
	{
		if (!m_isInitialized)
		{
			Initialize(screenWidth, screenHeight);
			return;
		}

		for (int i = 0; i < eGlobalTexture::TEXTURE_COUNT; ++i)
		{
			GetGlobalTexture(static_cast<eGlobalTexture>(i))->Release();

			D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_UNKNOWN, screenWidth, screenHeight, 1, 1);
			UINT shaderComponentAlignment = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			const char* name = GetGlobalTextureDetails(static_cast<eGlobalTexture>(i), textureDesc);

			D3D12_SAMPLER_DESC samplerDesc = CreateGlobalSamplerDesc();

			TextureBuffer* globalTexture = AllocTextureBuffer(
				&idStr(name), textureDesc, samplerDesc,
				shaderComponentAlignment, D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON,
				m_textures.size() <= i ? -1 : i);

		}
	}

	void TextureManager::RecordSamplerForFrame(const int frameIndex, const UINT textureIndex)
	{
		assert(textureIndex < BINDLESS_TEXTURE_COUNT);
		assert(m_nextSamplerLocation < SAMPLERS_ENTRIES_HEAP_SIZE);

		BindlessSamplerIndex& index = m_samplers[textureIndex];
		if (frameIndex != index.frame)
		{
			index.samplerMapIndex = m_nextSamplerLocation;
			index.frame = frameIndex;

			D3D12_SAMPLER_DESC& samplerDesc = GetTextureBuffer(textureIndex)->m_samplerDesc;
			const SamplerKey samplerKey = GetSamplerKey(samplerDesc);

			// Check if a matching sampler already exists for the frame.
			int samplerIndex = std::find_if(&m_samplerHash[0], &m_samplerHash[m_nextSamplerLocation], [&samplerKey](const SamplerKey& checkKey)
			{
				return IsEqualGUID(samplerKey, checkKey);
			}) - &m_samplerHash[0];

			if(samplerIndex < m_nextSamplerLocation)
			{
				index.samplerMapIndex = samplerIndex;
			}
			else
			{
				m_samplerHash[m_nextSamplerLocation] = samplerKey;

				// Place the sampler on the GPU
				HeapDescriptorManager* heapManager = GetDescriptorManager();
				D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle = heapManager->GetCPUDescriptorHandle(eHeapDescriptorSamplerEntries, m_nextSamplerLocation);
				Device::GetDevice()->CreateSampler(&samplerDesc, samplerHandle);

				++m_nextSamplerLocation;
			}
		}
	}

	void TextureManager::StoreFrameSamplers()
	{
		ResourceManager& resourceManager = *DX12Rendering::GetResourceManager();
		const size_t samplerSize = sizeof(m_samplers);

		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(samplerSize);
		resourceManager.FillConstantBuffer(buffer, m_samplers);

		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetDescriptorManager()->GetCPUDescriptorHandle(eHeapDescriptorTextureConstants, 0); // We use 0, as this will always be the first constant object.

		Device::GetDevice()->CreateConstantBufferView(&buffer.bufferLocation, descriptorHandle);
	}

	void TextureManager::SetTextureToDefault(UINT textureIndex)
	{
		HeapDescriptorManager* heapManager = GetDescriptorManager();
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		auto defaultTexture = GetGlobalTexture(eGlobalTexture::ALBEDO); // TODO: Change to a default mangenta for debugging

		{
			// Setup the texture
			D3D12_CPU_DESCRIPTOR_HANDLE textureHandle = heapManager->GetCPUDescriptorHandle(eHeapDescriptorTextureEntries, textureIndex);

			device->CreateShaderResourceView(defaultTexture->resource.Get(), &defaultTexture->textureView, textureHandle);
		}
	}

	void TextureManager::Clear() {
		for (TextureBuffer* texture : m_textures)
		{
			FreeTextureBuffer(texture);
		}

		m_textures.empty();
	}

	TextureBuffer* TextureManager::GetGlobalTexture(eGlobalTexture textureId)
	{
		size_t textureIndex = static_cast<size_t>(textureId);

		if (textureIndex >= m_textures.size())
		{
			return nullptr;
		}

		return m_textures.at(textureIndex);
	}

#pragma region TextureManager
	TextureManager* m_textureManager = nullptr;

	TextureManager* GetTextureManager()
	{
		if (m_textureManager == nullptr)
		{
			m_textureManager = new TextureManager();
		}

		return m_textureManager;
	}

	void DestroyTextureManager()
	{
		if (m_textureManager != nullptr)
		{
			delete m_textureManager;
			m_textureManager = nullptr;
		}
	}

	bool TextureManager::SetTextureCopyState(TextureBuffer* buffer, DX12Rendering::Commands::CommandList* commandList) const{
		return SetTextureState(buffer, D3D12_RESOURCE_STATE_COPY_DEST, commandList);
	}

	bool TextureManager::SetTexturePixelShaderState(TextureBuffer* buffer, DX12Rendering::Commands::CommandList* commandList) const {
		return SetTextureState(buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, commandList);
	}

	bool TextureManager::SetTextureState(TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList) const {
		if (buffer == nullptr) {
			return false;
		}

		// TODO: Check for valid state transitions.
		D3D12_RESOURCE_BARRIER transition = {};
		if (!buffer->TryTransition(usageState, &transition))
		{
			return false;
		}

		// For now we transition the entire resource.
		commandList->CommandResourceBarrier(1, &transition);

		return true; // State has changed.
	}

	void TextureManager::StartTextureWrite(TextureBuffer* buffer) {
		auto copyCommands = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY);
		auto commandList = copyCommands->RequestNewCommandList();

		SetTextureCopyState(buffer, commandList);

		DX12Rendering::CaptureEventStart(commandList, "StartTextureWrite");

		commandList->Close();
	}

	bool TextureManager::EndTextureWrite(TextureBuffer* buffer) {
		auto copyCommands = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY);
		auto commandList = copyCommands->RequestNewCommandList();

		DX12Rendering::CaptureEventEnd(commandList);
		commandList->Close();

		copyCommands->Execute();

		// Clean out the temp images
		m_tempImages.clear();

		return true;
	}

	byte* TextureManager::CreateTemporaryImageStorage(const UINT imageSize)
	{
		assert(imageSize >= 2);
		// TODO: Remove and just use unique_ptr
		auto data = std::make_unique<byte[]>(DX12_ALIGN(imageSize, 512));
		m_tempImages.push_back(std::move(data));

		return m_tempImages.back().get();
	}

	int TextureManager::StoreTextureEntry(TextureBuffer* buffer, int index)
	{
		// Setup the heap index data
		assert(index < (int)m_textures.size());

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		if (index < 0)
		{
			// Find the first free index. If none exists keep the index at -1
			for (int i = 0; i < m_textures.size(); ++i)
			{
				if (m_textures[i] == nullptr)
				{
					index = i;
					break;
				}
			}
		}

		if (index < 0)
		{
			m_textures.push_back(buffer);
			index = m_textures.size() - 1;
		}
		else
		{
			m_textures[index] = buffer;
		}

		buffer->m_heapIndex = index;

		if (index < BINDLESS_TEXTURE_COUNT)
		{
			HeapDescriptorManager* heapManager = GetDescriptorManager();

			{
				// Setup the texture
				D3D12_CPU_DESCRIPTOR_HANDLE textureHandle = heapManager->GetCPUDescriptorHandle(eHeapDescriptorTextureEntries, index);

				device->CreateShaderResourceView(buffer->resource.Get(), &buffer->textureView, textureHandle);
			}
		}

		return index;
	}

	TextureBuffer* TextureManager::GenerateFromExistingResource(const DX12Rendering::Resource* resource, D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
	{
		// Create the buffer
		TextureBuffer* buffer = new TextureBuffer(L"ResourceTexture");

		if (buffer->AttachToResource(resource, textureDesc, samplerDesc, srvDesc))
		{
			StoreTextureEntry(buffer, -1);

			return buffer;
		}

		return nullptr;
	}

	TextureBuffer* TextureManager::AllocTextureBuffer(const idStr* name, D3D12_RESOURCE_DESC& textureDesc, D3D12_SAMPLER_DESC& samplerDesc, const UINT shaderComponentMapping, D3D12_RESOURCE_STATES resourceState, int index)
	{
		// Create the name
		wchar_t wname[256];
		wsprintfW(wname, L"Texture: %hs", name->c_str());

		// Create the buffer
		TextureBuffer* buffer = new TextureBuffer(wname);

		// Create the Shader Resource View
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = shaderComponentMapping;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = textureDesc.DepthOrArraySize == 6 ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;

		if (buffer->Build(textureDesc, samplerDesc, srvDesc, resourceState))
		{
			StoreTextureEntry(buffer, index);

			return buffer;
		}

		return nullptr;
	}

	void TextureManager::FreeTextureBuffer(TextureBuffer* buffer) {
		if (buffer != nullptr) {
			const Commands::FenceValue fence = buffer->GetLastFenceValue();
			Commands::GetCommandManager(fence.commandList)->WaitOnFence(fence);

			SetTextureToDefault(buffer->m_heapIndex);

			m_textures[buffer->m_heapIndex] = nullptr;

			delete(buffer);
		}
	}

	void TextureManager::SetTextureContent(TextureBuffer* buffer, const UINT resourceIndex, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image) {
		if (buffer != nullptr && buffer->IsReady())
		{
			auto copyManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY);
			auto copyCommands = copyManager->RequestNewCommandList();
			
			SetTextureCopyState(buffer, copyCommands);

			ScratchBuffer& uploadHeap = m_textureUploadHeap;

			UINT64 intermediateOffset;
			if (!uploadHeap.RequestSpace(copyCommands, imageSize, intermediateOffset, true))
			{
				copyCommands->Close();
				copyManager->Execute();

				uploadHeap.WaitForLastFenceToComplete(); // Since we are not streaming in textures, we'll forcfully wait for our upload.

				copyCommands = copyManager->RequestNewCommandList();
			}

			copyCommands->AddCommandAction([image, bytesPerRow, imageSize, mipLevel, resourceIndex, intermediateOffset, &uploadHeap, &buffer](ID3D12GraphicsCommandList4* commandList)
			{
				D3D12_SUBRESOURCE_DATA textureData = {};
				textureData.pData = image;
				textureData.RowPitch = bytesPerRow;
				textureData.SlicePitch = imageSize;

				const UINT subresource = D3D12CalcSubresource(mipLevel, resourceIndex, 0, buffer->m_textureDesc.MipLevels, buffer->m_textureDesc.DepthOrArraySize);

				UpdateSubresources(commandList, buffer->resource.Get(), uploadHeap.resource.Get(), intermediateOffset, subresource, 1, &textureData);
			});

			buffer->SetLastFenceValue(copyCommands->AddPostFenceSignal());

			copyCommands->Close();
		}
		else
		{
			// TODO: Fix on null buffer.
		}
	}
#pragma engregion
}