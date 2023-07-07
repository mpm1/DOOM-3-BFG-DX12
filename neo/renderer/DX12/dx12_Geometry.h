#ifndef __DX12_GEOMETRY_H__
#define __DX12_GEOMETRY_H__

#include "./dx12_global.h"
#include "./dx12_resource.h"

namespace DX12Rendering
{
	namespace Geometry
	{
		struct GeometryResource : public Resource
		{
			GeometryResource(const LPCWSTR name) :
				Resource(name)
			{}

			bool Map(const D3D12_RANGE* pReadRange, void** ppData);
			bool Unmap(const D3D12_RANGE* pWrittenRange);
		};

		struct VertexBuffer : GeometryResource
		{
			VertexBuffer(const UINT numBytes, const LPCWSTR name);

			const D3D12_VERTEX_BUFFER_VIEW* GetView() { return &m_vertexBufferView; }
		private:
			D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
		};

		struct IndexBuffer : GeometryResource
		{
			IndexBuffer(const UINT numBytes, const LPCWSTR name);

			const D3D12_INDEX_BUFFER_VIEW* GetView() { return &m_indexBufferView; }
			const UINT GetIndexCount() { return m_indexCount; }
		private:
			D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
			UINT m_indexCount;
		};

		struct JointBuffer : GeometryResource
		{
			JointBuffer(const UINT entrySize, const UINT numBytes, const LPCWSTR name);

			const UINT* GetSize() { return &m_entrySizeInBytes; }
		private:
			UINT m_entrySizeInBytes;
		};
	}
}

#endif