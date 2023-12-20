#pragma hdrstop


#include "../tr_local.h"

#include "./dx12_TextureManager.h"

namespace DX12Rendering
{
	bool TextureBuffer::Build(D3D12_RESOURCE_DESC& textureDesc, D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc, D3D12_RESOURCE_STATES resourceState)
	{
		Release();

		m_textureDesc = textureDesc;

		Allocate(textureDesc, resourceState, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT));

		m_lastTransitionState = resourceState;
		textureView = srcDesc;

		return true;
	}

	TextureManager::TextureManager() :
		m_textureUploadHeap(3840*2160*4*16, 512, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, L"Texture Buffer Upload Resource Heap")
	{

	}

	TextureManager::~TextureManager() {
		Clear();
	}

	void TextureManager::Initialize(uint screenWidth, uint screenHeight) {
		// Create the basic entries for the global textures
		for (int i = 0; i < eGlobalTexture::TEXTURE_COUNT; ++i)
		{
			//TODO: Remove old texture

			D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_UNKNOWN, screenWidth, screenHeight, 1, 1);
			UINT shaderComponentAlignment = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			char* name;

			switch (i)
			{
			case eGlobalTexture::DEPTH_TEXTURE:
				name = "depth_texture";
				textureDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				break;

			case eGlobalTexture::VIEW_DEPTH:
				name = "view_depth";
				textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
				break;

			case eGlobalTexture::WORLD_NORMALS:
				name = "world_normals";
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

			TextureBuffer* globalTexture = dxRenderer.GetTextureManager()->AllocTextureBuffer(&idStr(name), textureDesc, shaderComponentAlignment, D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON);
			
			if(m_textures.size() <= i)
			{ 
				m_textures.push_back(globalTexture);
			}
			else
			{
				// TODO: replace
			}
		}
	}

	TextureBuffer* GetGlobalTexture(eGlobalTexture textureId);

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

	bool TextureManager::SetTextureCopyState(TextureBuffer* buffer, const UINT mipLevel) const{
		auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);
		return SetTextureState(buffer, D3D12_RESOURCE_STATE_COPY_DEST, copyCommands, mipLevel);
	}

	bool TextureManager::SetTexturePixelShaderState(TextureBuffer* buffer, const UINT mipLevel) const {
		auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
		return SetTextureState(buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, commandList);
	}

	bool TextureManager::SetTextureStates(TextureBuffer** buffers, UINT bufferCount, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList, const UINT mipLevel) const
	{
		if (buffers == nullptr)
		{
			return false;
		}

		/*std::vector<D3D12_RESOURCE_BARRIER> transitions = {};
		transitions.reserve(bufferCount);*/

		for (UINT index = 0; index < bufferCount; ++index)
		{
			if (buffers[index]->m_lastTransitionState != usageState) {
				/*transitions.push_back(CD3DX12_RESOURCE_BARRIER::Transition(buffers[index]->resource.Get(), buffers[index]->m_lastTransitionState, usageState, mipLevel));
				*/

				commandList->CommandResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(buffers[index]->resource.Get(), buffers[index]->m_lastTransitionState, usageState, mipLevel));
				buffers[index]->m_lastTransitionState = usageState;
			}
		}

		/*if (transitions.size() > 0)
		{
			commandList->CommandResourceBarrier(transitions.size(), std::move(transitions.data()));
		}*/

		return true;
	}

	bool TextureManager::SetTextureState(TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList, const UINT mipLevel) const {
		if (buffer == nullptr) {
			return false;
		}

		if (buffer->m_lastTransitionState == usageState) {
			return false;
		}

		// TODO: Check for valid state transitions.
		commandList->CommandResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(buffer->resource.Get(), buffer->m_lastTransitionState, usageState, mipLevel));
		buffer->m_lastTransitionState = usageState;

		return true; // State has changed.
	}

	void TextureManager::StartTextureWrite(TextureBuffer* buffer) {
		auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);
		copyCommands->Reset();

		DX12Rendering::CaptureEventStart(copyCommands, "StartTextureWrite");
	}

	void TextureManager::EndTextureWrite(TextureBuffer* buffer) {
		auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);
		copyCommands->AddPreFenceWait(&buffer->fence);
		copyCommands->AddPostFenceSignal(&buffer->fence);
		copyCommands->Cycle();

		DX12Rendering::CaptureEventEnd(copyCommands);
	}

	TextureBuffer* TextureManager::AllocTextureBuffer(const idStr* name, D3D12_RESOURCE_DESC& textureDesc, const UINT shaderComponentMapping, D3D12_RESOURCE_STATES resourceState) 
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

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

		if (buffer->Build(textureDesc, srvDesc, resourceState))
		{
			return buffer;
		}

		return nullptr;
	}

	void TextureManager::FreeTextureBuffer(TextureBuffer* buffer) {
		if (buffer != nullptr) {
			buffer->fence.Wait();

			delete(buffer);
		}
	}

	void TextureManager::SetTextureContent(TextureBuffer* buffer, const UINT resourceIndex, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image) {
		if (buffer != nullptr && buffer->IsReady())
		{
			auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);

			SetTextureCopyState(buffer, mipLevel);

			UINT64 intermediateOffset;
			if (!m_textureUploadHeap.RequestSpace(copyCommands, imageSize, intermediateOffset, true))
			{
				copyCommands->Cycle();
				m_textureUploadHeap.fence.Wait(); // Since we are not streaming in textures, we'll forcfully wait for our upload.
			}

			copyCommands->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
			{
				D3D12_SUBRESOURCE_DATA textureData = {};
				textureData.pData = image;
				textureData.RowPitch = bytesPerRow;
				textureData.SlicePitch = imageSize;

				const UINT subresource = D3D12CalcSubresource(mipLevel, resourceIndex, 0, buffer->m_textureDesc.MipLevels, buffer->m_textureDesc.DepthOrArraySize);

				UpdateSubresources(commandList, buffer->resource.Get(), m_textureUploadHeap.resource.Get(), intermediateOffset, subresource, 1, &textureData);
			});
		}
		else
		{
			// TODO: Fix on null buffer.
		}
	}
}