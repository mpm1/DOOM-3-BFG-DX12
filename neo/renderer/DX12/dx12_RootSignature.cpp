#pragma hdrstop

#include "./dx12_RootSignature.h"

DX12RootSignature::DX12RootSignature(ID3D12Device5* device, const size_t constantBufferSize)
	: m_device(device)
{
	CreateRootSignature();
	CreateCBVHeap(constantBufferSize);
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
	CD3DX12_DESCRIPTOR_RANGE1 descriptorTableRanges[2];
	descriptorTableRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
	descriptorTableRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 1);
	rootParameters[0].InitAsDescriptorTable(2, &descriptorTableRanges[0]);

	CD3DX12_DESCRIPTOR_RANGE1 jointDescriptorTableRanges[1];
	jointDescriptorTableRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
	rootParameters[1].InitAsDescriptorTable(1, &jointDescriptorTableRanges[0]);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler[2];
	staticSampler[0].Init(0, D3D12_FILTER_ANISOTROPIC); // Base Sampler
	staticSampler[1].Init(1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // Light projection sampler

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(2, rootParameters, 2, &staticSampler[0], rootSignatureFlags);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	DX12Rendering::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error));

	DX12Rendering::ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void DX12RootSignature::CreateCBVHeap(const size_t constantBufferSize) {
	// Create the buffer size.
	constexpr UINT resourceAlignment = (1024 * 64) - 1; // Resource must be a multible of 64KB
	const UINT entrySize = (constantBufferSize + 255) & ~255; // Size is required to be 256 byte aligned
	const UINT heapSize = ((entrySize * MAX_OBJECT_COUNT) + resourceAlignment) & ~resourceAlignment;
	WCHAR heapName[30];

	// Create Descriptor Heaps
	{
		// Describe and create the constant buffer view (CBV) descriptor for each frame
		for (UINT frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex) {
			// Create the CBV Heap
			D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
			cbvHeapDesc.NumDescriptors = MAX_HEAP_INDEX_COUNT;
			cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			DX12Rendering::ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap[frameIndex])));

			// Create the Constant buffer heap for each frame
			DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr, // Currently not clear value needed
				IID_PPV_ARGS(&m_cbvUploadHeap[frameIndex])
			));

			wsprintfW(heapName, L"CBV Upload Heap %d", frameIndex);
			m_cbvUploadHeap[frameIndex]->SetName(heapName);

			wsprintfW(heapName, L"CBV Heap %d", frameIndex);
			m_cbvHeap[frameIndex]->SetName(heapName);
		}

		m_cbvHeapIncrementor = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

void DX12RootSignature::OnDestroy()
{
}

void DX12RootSignature::BeginFrame(UINT frameIndex)
{
	assert(frameIndex >= 0 && frameIndex < DX12_FRAME_COUNT, "A positive frame index less than the specified Frame Count must be defined.");

	m_cbvHeapIndex = 0;
}

D3D12_CONSTANT_BUFFER_VIEW_DESC DX12RootSignature::SetJointDescriptorTable(DX12Rendering::Geometry::JointBuffer* buffer, UINT jointOffset, UINT frameIndex, DX12Rendering::Commands::CommandList* commandList) {
	assert(m_cbvHeapIndex < MAX_HEAP_INDEX_COUNT);

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvHeap[frameIndex]->GetCPUDescriptorHandleForHeapStart(), m_cbvHeapIndex, m_cbvHeapIncrementor);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = buffer->resource->GetGPUVirtualAddress() + jointOffset;
	cbvDesc.SizeInBytes = *buffer->GetSize();
	m_device->CreateConstantBufferView(&cbvDesc, descriptorHandle);

	UINT heapIndex = m_cbvHeapIndex;
	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
	{
		const CD3DX12_GPU_DESCRIPTOR_HANDLE tableHandle(m_cbvHeap[frameIndex]->GetGPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);
		commandList->SetGraphicsRootDescriptorTable(1, tableHandle);
	});

	++m_cbvHeapIndex;
	return cbvDesc;
}

D3D12_CONSTANT_BUFFER_VIEW_DESC DX12RootSignature::SetCBVDescriptorTable(const size_t constantBufferSize, XMFLOAT4* constantBuffer, UINT objectIndex, UINT frameIndex, DX12Rendering::Commands::CommandList* commandList) {
	// Copy the CBV value to the upload heap
	UINT8* buffer;
	const UINT bufferSize = ((constantBufferSize + 255) & ~255);
	UINT offset = bufferSize * objectIndex; // Each entry is 256 byte aligned.
	CD3DX12_RANGE readRange(offset, bufferSize);

	DX12Rendering::ThrowIfFailed(m_cbvUploadHeap[frameIndex]->Map(0, &readRange, reinterpret_cast<void**>(&buffer)));
	memcpy(&buffer[offset], constantBuffer, constantBufferSize);
	m_cbvUploadHeap[frameIndex]->Unmap(0, &readRange);

	// Create the constant buffer view for the object
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvHeap[frameIndex]->GetCPUDescriptorHandleForHeapStart(), m_cbvHeapIndex, m_cbvHeapIncrementor);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cbvUploadHeap[frameIndex]->GetGPUVirtualAddress() + offset;
	cbvDesc.SizeInBytes = bufferSize;
	m_device->CreateConstantBufferView(&cbvDesc, descriptorHandle);

	// Define the Descriptor Table to use.
	UINT heapIndex = m_cbvHeapIndex;
	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
	{
		const CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorTableHandle(m_cbvHeap[frameIndex]->GetGPUDescriptorHandleForHeapStart(), heapIndex, m_cbvHeapIncrementor);
		commandList->SetGraphicsRootDescriptorTable(0, descriptorTableHandle);
	});

	++m_cbvHeapIndex;
	return cbvDesc;
}

DX12Rendering::TextureBuffer* DX12RootSignature::SetTextureRegisterIndex(UINT textureIndex, DX12Rendering::TextureBuffer* texture, UINT frameIndex, DX12Rendering::Commands::CommandList* commandList) {
	CD3DX12_CPU_DESCRIPTOR_HANDLE textureHandle(m_cbvHeap[frameIndex]->GetCPUDescriptorHandleForHeapStart(), m_cbvHeapIndex, m_cbvHeapIncrementor);
	m_device->CreateShaderResourceView(texture->resource.Get(), &texture->textureView, textureHandle);

	++m_cbvHeapIndex;
	//m_copyCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(currentTexture->textureBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));

	return texture;
}