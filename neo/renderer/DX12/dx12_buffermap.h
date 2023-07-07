#ifndef __DX12_BUFFERMAP_H__
#define __DX12_BUFFERMAP_H__

#include "dx12_global.h"
#include "dx12_Geometry.h"
#include <comdef.h>

namespace DX12Rendering
{
//	template<typename _bufferType_>
//	struct BufferEntity
//	{
//		_bufferType_ buffer;
//		UINT index;
//		UINT size;
//		qhandle_t handle;
//	};
//
//	template<typename _bufferType_> 
//	class BufferMap;
//
//	class VertexBufferMap;
//
//	class IndexBufferMap;
//}
//
///// <summary>
///// 
///// </summary>
///// <typeparam name="_bufferType_"></typeparam>
///// <param name="_byteSize_">The size of each entry</param>
///// <param name="_maxSize_">The total size in bytes availaible in the buffer.</param>
//template<typename _bufferType_>
//class DX12Rendering::BufferMap {
//public:
//	const UINT size;
//	const UINT byteSize;
//
//	/// <summary>
//	/// 
//	/// </summary>
//	/// <param name="maxSize">The maximum size in bytes for the buffer.</param>
//	/// <param name="byteSize">The minimum size of each entry. All entities must be divisible by this value.</param>
//	BufferMap(UINT maxSize, UINT byteSize) :
//		size(maxSize),
//		byteSize(byteSize) {};
//
//	~BufferMap() {};
//
//	BufferEntity<_bufferType_>* TryAndGetEntity(qhandle_t entityHandle)
//	{
//		WriteLock bufferLock(m_lock);
//
//		auto result = std::find_if(m_entities.begin(), m_entities.end(), [entityHandle](BufferEntity<_bufferType_>& entry) { return entry.handle == entityHandle; });
//
//		if (result != m_entities.end()) {
//			return result._Unwrapped();
//		}
//
//		return nullptr;
//	};
//
//	BufferEntity<_bufferType_>* AddOrUpdateEntity(ID3D12Device5* device, qhandle_t entityHandle, byte* data, UINT size) 
//	{
//		WriteLock bufferLock(m_lock);
//
//		BufferEntity<_bufferType_>* entity = TryAndGetEntity(entityHandle);
//
//		if (entity != nullptr) 
//		{
//			WriteData(data, entity->index, size);
//		}
//		else
//		{
//			entity = AddEntity(device, entityHandle, data, size);
//		}
//
//		return entity;
//	}
//
//	BufferEntity<_bufferType_>* AddEntity(ID3D12Device5* device, qhandle_t entityHandle, byte* data, UINT size)
//	{
//		if (IsBufferCreated()) {
//			CreateBuffer(device);
//		}
//
//		BufferEntity<_bufferType_>* buffer = nullptr;
//
//		WriteLock bufferLock(m_lock);
//
//		// Find a space large enough for the item
//		UINT start = 0;
//		for (auto it = std::begin(m_entities); it != std::end(m_entities); ++it)
//		{
//			UINT maxNextIndex = start + size;
//			BufferEntity<_bufferType_>& entity = *it;
//
//			if (entity.index > maxNextIndex)
//			{
//				// Create the entity and insert it inside the list.
//				buffer = std::move(&CreateChildBuffer(start, size));
//				m_entities.insert(it - 1, *buffer);
//
//				break;
//			}
//
//			start = entity.index + entity.size + 1;
//		}
//
//		if ((start + size) >= size)
//		{
//			// We can't find any space. Throw an error.
//			return nullptr;
//		}
//		else if (buffer == nullptr)
//		{
//			// Create the Entity View Structure and add it to the back of the vector.
//			buffer = std::move(&CreateChildBuffer(start, size));
//			m_entities.push_back(*buffer);
//		}
//
//		buffer->handle = entityHandle;		
//
//		WriteData(data, buffer->index, size);
//
//		return buffer;
//	};
//
//protected:
//	/// <summary>
//	/// Create the main buffer used to store the data.
//	/// </summary>
//	/// <param name="device"></param>
//	virtual void CreateBuffer(ID3D12Device5* device) {};
//
//	/// <summary>
//	/// Creates a child view to the buffer.
//	/// </summary>
//	/// <param name="start"></param>
//	/// <param name="size"></param>
//	/// <returns></returns>
//	virtual BufferEntity<_bufferType_> CreateChildBuffer(UINT start, UINT size) { return {}; };
//
//	/// <summary>
//	/// Writes the new data to the buffer.
//	/// </summary>
//	/// <param name="data"></param>
//	/// <param name="start"></param>
//	/// <param name="size"></param>
//	virtual void WriteData(byte* data, UINT start, UINT size) {};
//
//	/// <summary>
//	/// Determines whether or not the buffer has been created.
//	/// </summary>
//	/// <returns></returns>
//	virtual bool IsBufferCreated() { return false; }
//	
//	dx12_lock m_lock;
//	_bufferType_ m_buffer;
//
//private:
//	std::vector<BufferEntity<_bufferType_>> m_entities;
//};
//
//#pragma region BufferMapTemplates
//
//class DX12Rendering::VertexBufferMap : public BufferMap<DX12Rendering::Geometry::VertexBuffer>
//{
//public:
//	VertexBufferMap(UINT maxSize) : BufferMap<DX12Rendering::Geometry::VertexBuffer>(maxSize, sizeof(idDrawVert)) {};
//
//protected:
//	void CreateBuffer(ID3D12Device5* device)
//	{
//		DX12Rendering::ThrowIfFailed(device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(size),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&m_buffer.vertexBuffer)
//		));
//
//		m_buffer.vertexBufferView.BufferLocation = m_buffer.vertexBuffer->GetGPUVirtualAddress();
//		m_buffer.vertexBufferView.StrideInBytes = sizeof(idDrawVert); // TODO: Change to Doom vertex structure
//		m_buffer.vertexBufferView.SizeInBytes = size;
//	}
//
//	BufferEntity<DX12VertexBuffer> CreateChildBuffer(UINT start, UINT size)
//	{
//		BufferEntity<DX12VertexBuffer> result;
//
//		result.buffer.vertexBufferView.BufferLocation = m_buffer.vertexBuffer->GetGPUVirtualAddress() + start;
//		result.buffer.vertexBufferView.StrideInBytes = sizeof(idDrawVert); // TODO: Change to Doom vertex structure
//		result.buffer.vertexBufferView.SizeInBytes = size;
//
//		// TODO: align the size;
//		result.index = start;
//		result.size = size;
//
//		return result;
//	}
//
//	void WriteData(byte* data, UINT start, UINT size)
//	{
//		//TODO: Add syncronization.
//
//		UINT8* buffer = NULL;
//
//		// Map the buffer location
//		D3D12_RANGE readRange = { 0, 0 };
//		HRESULT hr = m_buffer.vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&buffer));
//		ThrowIfFailed(hr);
//
//		buffer = (byte*)buffer + start;
//
//		// Write
//		memcpy(buffer, data, size);
//
//		// Unmap the buffer
//		m_buffer.vertexBuffer->Unmap(0, nullptr);
//	}
//
//	bool IsBufferCreated() { 
//		return m_buffer.vertexBuffer != nullptr;
//	}
//};
//
//class DX12Rendering::IndexBufferMap : public BufferMap<DX12Rendering::Geometry::IndexBuffer>
//{
//public:
//	IndexBufferMap(UINT maxSize) : BufferMap<DX12Rendering::Geometry::IndexBuffer>(maxSize, sizeof(UINT16)) {};
//
//protected:
//	void CreateBuffer(ID3D12Device5* device)
//	{
//		DX12Rendering::ThrowIfFailed(device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(size),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&m_buffer.indexBuffer)
//		));
//
//		m_buffer.indexBufferView.BufferLocation = m_buffer.indexBuffer->GetGPUVirtualAddress();
//		m_buffer.indexBufferView.Format = DXGI_FORMAT_R16_UINT;
//		m_buffer.indexBufferView.SizeInBytes = size;
//	}
//
//	BufferEntity<DX12Rendering::Geometry::IndexBuffer> CreateChildBuffer(UINT start, UINT size)
//	{
//		BufferEntity<DX12Rendering::Geometry::IndexBuffer> result;
//
//		result.buffer.indexBufferView.BufferLocation = m_buffer.indexBuffer->GetGPUVirtualAddress() + start;
//		result.buffer.indexBufferView.Format = DXGI_FORMAT_R16_UINT;
//		result.buffer.indexBufferView.SizeInBytes = size;
//
//		// TODO: align the size;
//		result.index = start;
//		result.size = size;
//
//		return result;
//	}
//
//	void WriteData(byte* data, UINT start, UINT size) 
//	{
//		//TODO: Add syncronization.
//
//		UINT8* buffer = NULL;
//
//		// Map the buffer location
//		D3D12_RANGE readRange = { 0, 0 };
//		HRESULT hr = m_buffer.indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&buffer));
//		ThrowIfFailed(hr);
//
//		buffer = (byte*)buffer + start;
//
//		// Write
//		memcpy(buffer, data, size);
//
//		// Unmap the buffer
//		m_buffer.indexBuffer->Unmap(0, nullptr);
//	}
//
//	bool IsBufferCreated() {
//		return m_buffer.indexBuffer != nullptr;
//	}
};

#pragma endregion

#endif