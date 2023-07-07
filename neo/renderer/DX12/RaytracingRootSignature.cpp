#pragma hdrstop

namespace DX12Rendering {
	#include "./RaytracingRootSignature.h"

	RaytracingRootSignature::RaytracingRootSignature(UINT flags)
	{
		CD3DX12_ROOT_PARAMETER1 rootParameters[1];

		// Build the descriptor table.
		std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges;

		if (flags & READ_ENVIRONMENT > 0) {
			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/,
				1,
				0 /*t0*/,
				0,
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				1
			));

			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_CBV /*Camera constant buffer*/,
				1,
				0 /*b0*/,
				0,
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				2
			));
		}

		if (flags & WRITE_OUTPUT > 0) {
			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/,
				1 /*1 descriptor */,
				0 /*u0*/,
				0 /*use the implicit register space 0*/,
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				0 /*heap slot where the UAV is defined*/
			));
		}
		
		if (descriptorRanges.size() > 0) {
			rootParameters[0].InitAsDescriptorTable(descriptorRanges.size(), (D3D12_DESCRIPTOR_RANGE1*)descriptorRanges.data());

			CreateRootSignature(rootParameters, 1);
		}
		else {
			CreateRootSignature(nullptr, 0);
		}
	}

	RaytracingRootSignature::~RaytracingRootSignature() {
		//TODO: Finish this and replace CreateRaytracingRootSignature in dx12_raytracing.cpp
	}

	void RaytracingRootSignature::CreateRootSignature(D3D12_ROOT_PARAMETER1* parameters, UINT parameterCount)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		// Describe the raytracing root signature.
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(parameterCount, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		// Create the root signature
		ComPtr<ID3DBlob> signatureBlob;
		ComPtr<ID3DBlob> errorBlob;
		DX12Rendering::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob));

		DX12Rendering::ThrowIfFailed(device->CreateRootSignature(0, signatureBlob.Get()->GetBufferPointer(), signatureBlob.Get()->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}
}