#pragma hdrstop

#include "../tr_local.h"

#include "./dx12_Geometry.h"

namespace DX12Rendering {
	bool Geometry::GeometryResource::Map(const D3D12_RANGE* readRange, void** ppData)
	{
		if (WarnIfFailed(resource->Map(0, readRange, ppData)))
		{
			//TODO: set a write flag to keep track
			return true;
		}

		return false;
	}

	bool Geometry::GeometryResource::Unmap(const D3D12_RANGE* pWrittenRange)
	{
		//TODO: Check mapped flag
		resource->Unmap(0, pWrittenRange);

		if (state == ResourceState::Ready)
		{
			state = ResourceState::Dirty;
		}

		return true;
	}

#pragma region VertexBuffer
	Geometry::VertexBuffer::VertexBuffer(const UINT numBytes, const LPCWSTR name) :
		GeometryResource(name)
	{
		// Allocate at the start
		D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(numBytes);
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		Allocate(description, D3D12_RESOURCE_STATE_GENERIC_READ, heapProps);

		// Fill in the view
		m_vertexBufferView.BufferLocation = state == Unallocated ? NULL : resource->GetGPUVirtualAddress();
		m_vertexBufferView.SizeInBytes = numBytes;
		m_vertexBufferView.StrideInBytes = sizeof(idDrawVert); // TODO: Change to Doom vertex structure
	}
#pragma endregion

#pragma region IndexBuffer
	Geometry::IndexBuffer::IndexBuffer(const UINT numBytes, const LPCWSTR name) :
		GeometryResource(name),
		m_indexCount(numBytes / 16)
	{
		// Allocate at the start
		D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(numBytes);
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		Allocate(description, D3D12_RESOURCE_STATE_GENERIC_READ, heapProps);

		// Fill in the view
		m_indexBufferView.BufferLocation = state == Unallocated ? NULL : resource->GetGPUVirtualAddress();
		m_indexBufferView.SizeInBytes = numBytes;
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	}
#pragma endregion

#pragma region IndexBuffer
	Geometry::JointBuffer::JointBuffer(const UINT entrySize, const UINT numBytes, const LPCWSTR name) :
		GeometryResource(name),
		m_entrySizeInBytes(entrySize)
	{
		// Allocate at the start
		D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(numBytes);
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		Allocate(description, D3D12_RESOURCE_STATE_GENERIC_READ, heapProps);
	}
#pragma endregion
}