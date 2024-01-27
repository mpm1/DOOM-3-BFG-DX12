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
				DX12Rendering::e_RaytracingHeapIndex::SRV_TLAS
			));

			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV /* Depth Texture */,
				2, /* Two descriptors: depth, normal */
				1 /*t1*/,
				0,
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				DX12Rendering::e_RaytracingHeapIndex::SRV_DepthTexture // Starting texture
			));

			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV /* Texture Array */,
				DESCRIPTOR_TEXTURE_COUNT, /* Total number of possible textures */
				3 /*t3*/,
				0, /* space0. We will define different spaces if we want to use more than texture 2D, but use the same data. */
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				DX12Rendering::e_RaytracingHeapIndex::SRV_TextureArray // Starting texture
			));

			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_CBV /*Camera constant buffer*/,
				1,
				0 /*b0*/,
				0,
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				DX12Rendering::e_RaytracingHeapIndex::CBV_CameraProperties
			));
		}

		if (flags & WRITE_OUTPUT > 0) {
			// Shadow Mask Buffer
			descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
				D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/,
				2 /*2 descriptors */,
				0 /*u0, u1*/,
				0 /*use the implicit register space 0*/,
				D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
				DX12Rendering::e_RaytracingHeapIndex::UAV_ShadowMap /*heap slot where the first UAV is defined*/
			));

			// Diffuse Buffer
			//descriptorRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(
			//	D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/,
			//	1 /*1 descriptor */,
			//	1 /*u1*/,
			//	0 /*use the implicit register space 0*/,
			//	D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
			//	DX12Rendering::e_RaytracingHeapIndex::UAV_DiffuseMap /*heap slot where the UAV is defined*/
			//));
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

		// Set Samplers
		const UINT samplerCount = 2;
		CD3DX12_STATIC_SAMPLER_DESC staticSampler[samplerCount];
		staticSampler[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // Point Sampler
		staticSampler[1].Init(1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // Light projection sampler

		// Describe the raytracing root signature.
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(parameterCount, parameters, samplerCount, &staticSampler[0], D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		// Create the root signature
		ComPtr<ID3DBlob> signatureBlob;
		ComPtr<ID3DBlob> errorBlob;
		DX12Rendering::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob));

		DX12Rendering::ThrowIfFailed(device->CreateRootSignature(0, signatureBlob.Get()->GetBufferPointer(), signatureBlob.Get()->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}
}