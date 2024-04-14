#ifndef __DX12_ROOT_SIGNATURE_H__
#define __DX12_ROOT_SIGNATURE_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"
#include "./dx12_Geometry.h"
#include "./dx12_TextureManager.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering
{
	enum eRootSignatureEntry
	{
		eModelCBV = 0,
		eJointCBV,
		eSurfaceCBV,
		eTesxture0SRV,
		eTesxture1SRV,
		eTesxture2SRV,
		eTesxture3SRV,
		eTesxture4SRV,
		eTesxture5SRV,
	};

	class DX12RootSignature;
}

class DX12Rendering::DX12RootSignature {
public:
	DX12RootSignature(ID3D12Device5* device);
	~DX12RootSignature();

	ID3D12RootSignature* GetRootSignature() { return m_rootSignature.Get(); }
	ID3D12DescriptorHeap* GetCBVHeap() { return m_cbvHeap.Get(); }

	/// <summary>
	/// Initializes the RootSignature for the current frame and resets cbvHeapIndex.
	/// </summary>
	/// <param name="frameIndex"></param>
	void BeginFrame(UINT frameIndex);

	void SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList);

	void SetConstantBufferView(const UINT objectIndex, const eRootSignatureEntry constantLocation, const ConstantBuffer& buffer);

	DX12Rendering::TextureBuffer* SetTextureRegisterIndex(UINT objectIndex, UINT textureIndex, DX12Rendering::TextureBuffer* texture, DX12Rendering::Commands::CommandList* commandList);

	const UINT RequestNewObjectIndex() {
		UINT result = m_nextObjectIndex;

		if ((++m_nextObjectIndex) >= MAX_OBJECT_COUNT) { m_nextObjectIndex = 0; }

		return result;
	}
private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12RootSignature> m_rootSignature;

	ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
	UINT m_cbvHeapIncrementor;

	UINT m_nextObjectIndex;

	void CreateRootSignature();
	void CreateCBVHeap();

	void OnDestroy();

	const UINT GetHeapIndex(UINT objectIndex, UINT offset) const { return (objectIndex * MAX_DESCRIPTOR_COUNT) + offset;  }
};

#endif