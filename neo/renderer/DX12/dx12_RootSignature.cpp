#pragma hdrstop

#include "./dx12_RootSignature.h"

HRESULT DX12CreateRootSignature(ID3D12Device5* device, REFIID riid, _COM_Outptr_  void** ppvRootSignature) {
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

	CD3DX12_STATIC_SAMPLER_DESC staticSampler[1];
	staticSampler[0].Init(0, D3D12_FILTER_ANISOTROPIC);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(2, rootParameters, 1, &staticSampler[0], rootSignatureFlags);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	HRESULT result = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
	if (FAILED(result)) {
		return result;
	}

	return device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), riid, ppvRootSignature);
}

#pragma region Joints
HRESULT DX12AllocJointBuffer(ID3D12Device* device, DX12JointBuffer* buffer, const UINT numBytes, LPCWSTR heapName) {
	// Create the buffer size.
	constexpr UINT resourceAlignment = (1024 * 64) - 1; // Resource must be a multible of 64KB
	constexpr UINT entrySize = ((sizeof(float) * 4 * 404) + 255) & ~255; // (Size of float4 * maxFloatAllowed) that's 256 byts aligned.
	const UINT heapSize = (numBytes + resourceAlignment) & ~resourceAlignment;

	// TODO: SET THIS FOR THE UPLOAD HEAP... THEN DO A SEPARATE JOINT HEAP JUST LIKE CBV
	HRESULT result = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, // Currently not clear value needed
		IID_PPV_ARGS(&buffer->jointBuffer)
	);

	if (FAILED(result)) {
		return result;
	}
	
	buffer->jointBuffer->SetName(heapName);
	buffer->entrySizeInBytes = entrySize;

	return result;
}

void DX12SetJointDescriptorTable(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12DescriptorHeap* jointHeap, DX12JointBuffer* buffer, UINT jointOffset, UINT heapIndex, UINT heapIncrementor) {
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(jointHeap->GetCPUDescriptorHandleForHeapStart(), heapIndex, heapIncrementor);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = buffer->jointBuffer->GetGPUVirtualAddress() + jointOffset;
	cbvDesc.SizeInBytes = buffer->entrySizeInBytes;
	device->CreateConstantBufferView(&cbvDesc, descriptorHandle);

	const CD3DX12_GPU_DESCRIPTOR_HANDLE tableHandle(jointHeap->GetGPUDescriptorHandleForHeapStart(), heapIndex, heapIncrementor);
	commandList->SetGraphicsRootDescriptorTable(1, tableHandle);
}

#pragma endregion