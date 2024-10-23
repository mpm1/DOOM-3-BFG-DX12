#pragma hdrstop

#include "./dx12_RootSignature.h"

using namespace DX12Rendering;

DX12RootSignature::DX12RootSignature(ID3D12Device5* device)
	: m_device(device),
	m_nextObjectIndex(0)
{
}

DX12RootSignature::~DX12RootSignature()
{
	OnDestroy();
}

void DX12RootSignature::Initialize()
{
	CreateRootSignature();
}

ID3D12DescriptorHeap* DX12RootSignature::GetCBVHeap()
{
	return GetDescriptorManager()->GetCBVHeap();
}

void DX12RootSignature::GenerateRootSignature(CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC* rootSignatureDesc, const LPCWSTR name)
{
	assert(rootSignatureDesc != nullptr);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	DX12Rendering::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

	DX12Rendering::ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

	m_rootSignature->SetName(name);
}

void DX12RootSignature::BeginFrame(UINT frameIndex)
{
	assert(frameIndex >= 0 && frameIndex < DX12_FRAME_COUNT, "A positive frame index less than the specified Frame Count must be defined.");
}

void DX12RootSignature::SetConstantBufferView(const UINT objectIndex, const UINT constantIndex, const ConstantBuffer& buffer)
{
	UINT heapIndex = GetHeapIndex(objectIndex, constantIndex);
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetDescriptorManager()->GetCPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);

	m_device->CreateConstantBufferView(&buffer.bufferLocation, descriptorHandle);
}

void DX12RootSignature::SetUnorderedAccessView(const UINT objectIndex, const UINT constantIndex, DX12Rendering::Resource* resource)
{
	UINT heapIndex = GetHeapIndex(objectIndex, constantIndex);

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetDescriptorManager()->GetCPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);

	m_device->CreateUnorderedAccessView(resource->resource.Get(), nullptr, resource->GetUavDescriptorView(), descriptorHandle);
}

void DX12RootSignature::SetShaderResourceView(const UINT objectIndex, const UINT constantIndex, DX12Rendering::Resource* resource)
{
	UINT heapIndex = GetHeapIndex(objectIndex, constantIndex);

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetDescriptorManager()->GetCPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);

	m_device->CreateShaderResourceView(resource->resource.Get(), resource->GetSrvDescriptorView(), descriptorHandle);
}

void RenderRootSignature::CreateRootSignature()
{
	// Generate the root signature
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_ROOT_PARAMETER1 rootParameters[2];

	// Setup the descriptor table
	{
		CD3DX12_DESCRIPTOR_RANGE1 descriptorTableRanges[2];
		descriptorTableRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, CBV_REGISTER_COUNT, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
		descriptorTableRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, TEXTURE_REGISTER_COUNT, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, eTesxture0SRV /* First Texture */);
		rootParameters[0].InitAsDescriptorTable(2, &descriptorTableRanges[0]);
	}

	// Setup the texture table
	{
		DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();
		const D3D12_DESCRIPTOR_RANGE1* ranges = textureManager->GetDescriptorRanges();

		rootParameters[1].InitAsDescriptorTable(TextureManager::TEXTURE_SPACE_COUNT, ranges);
	}

	CD3DX12_STATIC_SAMPLER_DESC staticSampler[3];
	staticSampler[0].Init(0, D3D12_FILTER_ANISOTROPIC); // Base Sampler
	staticSampler[1].Init(1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // Light projection sampler
	staticSampler[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // For direct pixel access

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(2, &rootParameters[0], 3, &staticSampler[0], rootSignatureFlags);

	GenerateRootSignature(&rootSignatureDesc, L"Render Root Signature");
}

void RenderRootSignature::SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList)
{
	const UINT heapIndex = GetHeapIndex(objectIndex, 0);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle = GetDescriptorManager()->GetGPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);
	CD3DX12_GPU_DESCRIPTOR_HANDLE textureDescriptorHandle = GetDescriptorManager()->GetGPUDescriptorHandle(eHeapDescriptorTextureEntries, 0);

	// Define the Descriptor Table to use.
	commandList->AddCommandAction([gpuDescriptorHandle, textureDescriptorHandle](ID3D12GraphicsCommandList4* commandList)
	{
		commandList->SetGraphicsRootDescriptorTable(0, gpuDescriptorHandle);
		commandList->SetGraphicsRootDescriptorTable(1, textureDescriptorHandle);
	});
}

void RenderRootSignature::OnDestroy()
{
}

DX12Rendering::TextureBuffer* RenderRootSignature::SetTextureRegisterIndex(UINT objectIndex, UINT textureIndex, DX12Rendering::TextureBuffer* texture, DX12Rendering::Commands::CommandList* commandList) {
	UINT heapIndex = GetHeapIndex(objectIndex, textureIndex + eTesxture0SRV);

	HeapDescriptorManager* heapManager = GetDescriptorManager();

	CD3DX12_CPU_DESCRIPTOR_HANDLE textureHandle = heapManager->GetCPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);
	m_device->CreateShaderResourceView(texture->resource.Get(), &texture->textureView, textureHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = heapManager->GetGPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);
	texture->SetGPUDescriptorHandle(gpuHandle);

	//m_copyCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentTexture->textureBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
	return texture;
}

void ComputeRootSignature::CreateRootSignature()
{
	// Generate the root signature
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_ROOT_PARAMETER1 rootParameters[2];

	// Setup the descriptor table
	{
		CD3DX12_DESCRIPTOR_RANGE1 descriptorTableRanges[3];
		descriptorTableRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, CBV_REGISTER_COUNT, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
		descriptorTableRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, eTesxture0SRV /* First Texture */);
		descriptorTableRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, TEXTURE_REGISTER_COUNT - 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, eTesxture0SRV + 1 /* First Texture */);
		rootParameters[0].InitAsDescriptorTable(3, &descriptorTableRanges[0]);
	}

	// Setup the texture table
	{
		DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();
		const D3D12_DESCRIPTOR_RANGE1* ranges = textureManager->GetDescriptorRanges();

		rootParameters[1].InitAsDescriptorTable(TextureManager::TEXTURE_SPACE_COUNT, ranges);
	}

	CD3DX12_STATIC_SAMPLER_DESC staticSampler[3];
	staticSampler[0].Init(0, D3D12_FILTER_ANISOTROPIC); // Base Sampler
	staticSampler[1].Init(1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // Light projection sampler
	staticSampler[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // For direct pixel access

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(2, &rootParameters[0], 3, &staticSampler[0], rootSignatureFlags);

	GenerateRootSignature(&rootSignatureDesc, L"Compute Root Signature");
}

void ComputeRootSignature::SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList)
{
	const UINT heapIndex = GetHeapIndex(objectIndex, 0);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle = GetDescriptorManager()->GetGPUDescriptorHandle(GetCBVHeapPartition(), heapIndex);
	CD3DX12_GPU_DESCRIPTOR_HANDLE textureDescriptorHandle = GetDescriptorManager()->GetGPUDescriptorHandle(eHeapDescriptorTextureEntries, 0);

	ID3D12DescriptorHeap* heap = GetCBVHeap();

	ID3D12RootSignature* rootSignature = GetRootSignature();

	// Define the Descriptor Table to use.
	commandList->AddCommandAction([gpuDescriptorHandle, textureDescriptorHandle, heap, rootSignature](ID3D12GraphicsCommandList4* commandList)
	{
		commandList->SetComputeRootSignature(rootSignature);

		// Setup the initial heap location
		ID3D12DescriptorHeap* descriptorHeaps[1] = {
			heap,
		};
		commandList->SetDescriptorHeaps(1, descriptorHeaps);

		commandList->SetComputeRootDescriptorTable(0, gpuDescriptorHandle);
		commandList->SetComputeRootDescriptorTable(1, textureDescriptorHandle); 
	});
}

void ComputeRootSignature::OnDestroy()
{
}