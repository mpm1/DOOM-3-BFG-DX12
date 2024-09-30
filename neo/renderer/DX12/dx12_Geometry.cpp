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

		if (state == eResourceState::Ready)
		{
			state = eResourceState::Dirty;
		}

		return true;
	}

#pragma region VertexBuffer
	Geometry::VertexBuffer::VertexBuffer(const UINT numBytes, const LPCWSTR name, const bool isGPUWritable) :
		GeometryResource(name),
		m_vertexBufferView({}),
		m_uavBufferView({}),
		m_srvBufferView({})
	{
		// Allocate at the start
		D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(numBytes, isGPUWritable ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(isGPUWritable ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD);
		Allocate(description, isGPUWritable ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ, heapProps);

		// Fill in the view
		m_vertexBufferView.BufferLocation = state == Unallocated ? NULL : resource->GetGPUVirtualAddress();
		m_vertexBufferView.SizeInBytes = numBytes;
		m_vertexBufferView.StrideInBytes = sizeof(idDrawVert);

		m_uavBufferView.Format = description.Format;
		m_uavBufferView.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		m_uavBufferView.Buffer.FirstElement = 0;
		m_uavBufferView.Buffer.NumElements = numBytes / sizeof(idDrawVert);
		m_uavBufferView.Buffer.StructureByteStride = sizeof(idDrawVert);

		m_srvBufferView.Format = description.Format;
		m_srvBufferView.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		m_srvBufferView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		m_srvBufferView.Buffer.FirstElement = 0;
		m_srvBufferView.Buffer.NumElements = numBytes / sizeof(idDrawVert);
		m_srvBufferView.Buffer.StructureByteStride = sizeof(idDrawVert);
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