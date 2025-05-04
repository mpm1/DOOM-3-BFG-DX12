#ifndef __DX12_ROOT_SIGNATURE_H__
#define __DX12_ROOT_SIGNATURE_H__

#include "./dx12_global.h"
#include "./dx12_CommandList.h"
#include "./dx12_Geometry.h"
#include "./dx12_HeapDescriptorManager.h"
#include "./dx12_TextureManager.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering
{
	enum eRenderRootSignatureEntry
	{
		eModelCBV = 0,
		eJointCBV,
		eSurfaceCBV,
		eTextureCBV,
		eTesxture0SRV,
		eTesxture1SRV,
		eTesxture2SRV,
		eTesxture3SRV,
		eTesxture4SRV,
		eTesxture5SRV,
	};

	class DX12RootSignature
	{
	public:
		DX12RootSignature(ID3D12Device5* device);
		~DX12RootSignature();

		ID3D12RootSignature* GetRootSignature() { return m_rootSignature.Get(); }

		ID3D12DescriptorHeap* GetCBVHeap();
		ID3D12DescriptorHeap* GetSamplerHeap();

		/// <summary>
		/// Initializes the RootSignature for the current frame and resets cbvHeapIndex.
		/// </summary>
		/// <param name="frameIndex"></param>
		void BeginFrame(UINT frameIndex);

		virtual void SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList) = 0;
		virtual eHeapDescriptorPartition GetCBVHeapPartition() = 0;

		void SetConstantBufferView(const UINT objectIndex, const UINT constantIndex, const ConstantBuffer& buffer);

		void SetUnorderedAccessView(const UINT objectIndex, const UINT constantIndex, DX12Rendering::Resource* resource);
		void SetUnorderedAccessView(const UINT objectIndex, const UINT constantIndex, DX12Rendering::Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC& view);

		void SetShaderResourceView(const UINT objectIndex, const UINT constantIndex, DX12Rendering::Resource* resource);

		DX12Rendering::TextureBuffer* SetTextureRegisterIndex(UINT objectIndex, UINT textureIndex, DX12Rendering::TextureBuffer* texture, DX12Rendering::Commands::CommandList* commandList);

		const UINT RequestNewObjectIndex() {
			UINT result = m_nextObjectIndex;

			if ((++m_nextObjectIndex) >= MAX_OBJECT_COUNT) { m_nextObjectIndex = 0; }

			return result;
		}
	protected:
		ID3D12Device5* m_device;

		void GenerateRootSignature(CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC* rootSignatureDesc, const LPCWSTR name);
		const UINT GetHeapIndex(UINT objectIndex, UINT offset) const { return (objectIndex * MAX_DESCRIPTOR_COUNT) + offset; }

		void Initialize();
	private:
		ComPtr<ID3D12RootSignature> m_rootSignature;

		UINT m_nextObjectIndex;

		void CreateCBVHeap();

		virtual void CreateRootSignature() {};

		virtual void OnDestroy() {};
	};

	class RenderRootSignature;
	class ComputeRootSignature;
}

class DX12Rendering::RenderRootSignature : public DX12Rendering::DX12RootSignature
{
public:
	RenderRootSignature(ID3D12Device5* device) : DX12RootSignature(device) { Initialize(); };

	DX12Rendering::TextureBuffer* SetTextureRegisterIndex(UINT objectIndex, UINT textureIndex, DX12Rendering::TextureBuffer* texture, DX12Rendering::Commands::CommandList* commandList);

	void SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList);

	eHeapDescriptorPartition GetCBVHeapPartition() { return eHeapDescriptorRenderObjects; }
private:
	virtual void CreateRootSignature();

	virtual void OnDestroy();
};

class DX12Rendering::ComputeRootSignature : public DX12Rendering::DX12RootSignature
{
public:
	ComputeRootSignature(ID3D12Device5* device) : DX12RootSignature(device) { Initialize(); };

	void SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList);

	eHeapDescriptorPartition GetCBVHeapPartition() { return eHeapDescriptorComputeObjects; }
private:
	virtual void CreateRootSignature();

	virtual void OnDestroy();
};

#endif