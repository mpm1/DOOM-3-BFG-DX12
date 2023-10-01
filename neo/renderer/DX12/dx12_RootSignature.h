#ifndef __DX12_ROOT_SIGNATURE_H__
#define __DX12_ROOT_SIGNATURE_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"
#include "./dx12_Geometry.h"
#include "./dx12_TextureManager.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class DX12RootSignature {
public:
	DX12RootSignature(ID3D12Device5* device, const size_t constantBufferSize);
	~DX12RootSignature();

	ID3D12RootSignature* GetRootSignature() { return m_rootSignature.Get(); }
	ID3D12DescriptorHeap* GetCBVHeap() { return m_cbvHeap.Get(); }

	/// <summary>
	/// Initializes the RootSignature for the current frame and resets cbvHeapIndex.
	/// </summary>
	/// <param name="frameIndex"></param>
	void BeginFrame(UINT frameIndex);

	D3D12_CONSTANT_BUFFER_VIEW_DESC SetJointDescriptorTable(DX12Rendering::Geometry::JointBuffer* buffer, UINT jointOffset, DX12Rendering::Commands::CommandList* commandList);
	D3D12_CONSTANT_BUFFER_VIEW_DESC SetCBVDescriptorTable(const size_t constantBufferSize, XMFLOAT4* m_constantBuffer, UINT objectIndex, DX12Rendering::Commands::CommandList* commandList);
	DX12Rendering::TextureBuffer* SetTextureRegisterIndex(UINT textureIndex, DX12Rendering::TextureBuffer* texture, DX12Rendering::Commands::CommandList* commandList);
private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
	ComPtr<ID3D12Resource> m_cbvUploadHeap;
	UINT m_cbvHeapIncrementor;
	UINT m_cbvHeapIndex;

	void CreateRootSignature();
	void CreateCBVHeap(const size_t constantBufferSize);

	void OnDestroy();

	const UINT GetHeapIndex() const { return m_cbvHeapIndex;  }
	const UINT IncrementHeapIndex() {
		if ((++m_cbvHeapIndex) >= MAX_HEAP_INDEX_COUNT) { m_cbvHeapIndex %= MAX_HEAP_INDEX_COUNT; } return m_cbvHeapIndex;
	}
};

#endif