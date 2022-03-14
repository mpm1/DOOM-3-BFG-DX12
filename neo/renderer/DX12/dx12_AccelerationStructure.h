#ifndef __DX12_ACCELERATION_STRUCTURE_H__
#define __DX12_ACCELERATION_STRUCTURE_H__

#include <map>
#include <atomic>

#include "./dx12_global.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering 
{
	struct DX12AccelerationObject {
		D3D12_RAYTRACING_GEOMETRY_DESC* vertex_buffer;

		DX12AccelerationObject(D3D12_RAYTRACING_GEOMETRY_DESC* desc) :
			vertex_buffer(desc) {}
	};

	struct Instance {
		ID3D12Resource* bottomLevelAS;
		XMMATRIX		transformation;
		UINT			instanceId;
		UINT			hitGroupIndex; // Should this be stage index?
		//TODO: Add support for bone information.

		Instance(ID3D12Resource* blas) :
			bottomLevelAS(blas),
			transformation(),
			instanceId(0),
			hitGroupIndex(0) {}
	};

	/// <summary>
	/// The base structure tree for all objects being referenced in the scene.
	/// </summary>
	class BottomLevelAccelerationStructure;

	/// <summary>
	/// The structure defining which objects will be active in the scene and which shaders will be used.
	/// </summary>
	class TopLevelAccelerationStructure;
}

class DX12Rendering::BottomLevelAccelerationStructure {
public:
	using vertex_buffer_entry = std::pair<dxObjectIndex_t, DX12AccelerationObject>;

	BottomLevelAccelerationStructure();
	~BottomLevelAccelerationStructure();

	DX12AccelerationObject* AddAccelerationObject(const dxObjectIndex_t& key, DX12VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);
	DX12AccelerationObject* GetAccelerationObject(const dxObjectIndex_t& key);

	void Reset();
	void Generate(ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, UINT64 scratchBufferSize);
	void CalculateBufferSize(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags);
private:
	std::map<dxObjectIndex_t, DX12AccelerationObject> m_objectMap = {};
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_vertexBuffers = {};
	ComPtr<ID3D12Resource> m_result = nullptr;
	UINT64 m_resultSizeInBytes = 0;
	std::atomic_bool m_isDirty = true;
};

class DX12Rendering::TopLevelAccelerationStructure {
public:
	TopLevelAccelerationStructure(ID3D12Device5* device);
	~TopLevelAccelerationStructure();

	void AddInstance(const dxObjectIndex_t& index);
	void Reset(); // Clears the acceleration structure.
	void UpdateResources(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() { return m_result->GetGPUVirtualAddress(); }
private:
	ID3D12Device5* m_device;
	ComPtr<ID3D12Resource> m_result; // Top Level Acceleration Structure - Used for raytracing.
	ComPtr<ID3D12Resource> m_instanceDesc;
	std::vector<DX12Rendering::Instance> m_instances;

	void CacluateBufferSizes(UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize);
};
#endif