#pragma hdrstop

#include "./dx12_RootSignature.h"

using namespace DX12Rendering;

DX12RootSignature::DX12RootSignature(ID3D12Device5* device)
	: m_device(device),
	m_nextObjectIndex(0)
{
	CreateRootSignature();
	CreateCBVHeap();
}

DX12RootSignature::~DX12RootSignature()
{
	OnDestroy();
}

void DX12RootSignature::CreateRootSignature()
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

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	DX12Rendering::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

	DX12Rendering::ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void DX12RootSignature::CreateCBVHeap() {
	// Create the buffer size.
	WCHAR heapName[30];

	// Describe and create the constant buffer view (CBV) descriptor
	{

		// Create the CBV Heap
		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
		cbvHeapDesc.NumDescriptors = MAX_HEAP_INDEX_COUNT;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX12Rendering::ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));

		wsprintfW(heapName, L"CBV Heap %d", 1);
		m_cbvHeap->SetName(heapName);
	}

	m_cbvHeapIncrementor = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DX12RootSignature::OnDestroy()
{
}

void DX12RootSignature::BeginFrame(UINT frameIndex)
{
	assert(frameIndex >= 0 && frameIndex < DX12_FRAME_COUNT, "A positive frame index less than the specified Frame Count must be defined.");
}

void DX12RootSignature::SetConstantBufferView(const UINT objectIndex, const eRootSignatureEntry constantLocation, const ConstantBuffer& buffer)
{
	assert(constantLocation >= eRootSignatureEntry::eModelCBV && constantLocation < eRootSignatureEntry::eTesxture0SRV);

	UINT heapIndex = GetHeapIndex(objectIndex, constantLocation);

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);

	m_device->CreateConstantBufferView(&buffer.bufferLocation, descriptorHandle);
}

void DX12RootSignature::SetRootDescriptorTable(const UINT objectIndex, DX12Rendering::Commands::CommandList* commandList)
{
	UINT heapIndex = GetHeapIndex(objectIndex, 0);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);

	// Define the Descriptor Table to use.
	commandList->AddCommandAction([&gpuDescriptorHandle](ID3D12GraphicsCommandList4* commandList)
	{
		DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();

		commandList->SetGraphicsRootDescriptorTable(0, gpuDescriptorHandle);
		commandList->SetGraphicsRootDescriptorTable(1, textureManager->GetDescriptorHandle());
	});
}

DX12Rendering::TextureBuffer* DX12RootSignature::SetTextureRegisterIndex(UINT objectIndex, UINT textureIndex, DX12Rendering::TextureBuffer* texture, DX12Rendering::Commands::CommandList* commandList) {
	UINT heapIndex = GetHeapIndex(objectIndex, textureIndex + eTesxture0SRV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE textureHandle(m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);
	m_device->CreateShaderResourceView(texture->resource.Get(), &texture->textureView, textureHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);
	texture->SetGPUDescriptorHandle(gpuHandle);

	//m_copyCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentTexture->textureBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
	return texture;
}