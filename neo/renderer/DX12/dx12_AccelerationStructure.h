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
		UINT index;

		DX12AccelerationObject(D3D12_RAYTRACING_GEOMETRY_DESC* desc, UINT index) :
			vertex_buffer(desc),
			index(index) {}
	};

	struct Instance {
		XMMATRIX		transformation;
		UINT			instanceId;
		UINT			hitGroupIndex; // TODO: We will change this to point to the hitGroup in the stack that contains the normal map for the surface.
		//TODO: Add support for bone information.

		Instance(XMMATRIX transformation, UINT instanceId, UINT hitGroupIndex) :
			transformation(transformation),
			instanceId(instanceId),
			hitGroupIndex(hitGroupIndex)
		{}
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
	BottomLevelAccelerationStructure();
	~BottomLevelAccelerationStructure();

	DX12AccelerationObject* AddAccelerationObject(const dxHandle_t& key, DX12VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);
	DX12AccelerationObject* GetAccelerationObject(const dxHandle_t& key);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() { return m_result->GetGPUVirtualAddress(); }

	void Reset();
	void Generate(ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, UINT64 scratchBufferSize);
	void CalculateBufferSize(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags);
private:
	std::map<dxHandle_t, DX12AccelerationObject> m_objectMap = {};
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_vertexBuffers = {};
	ComPtr<ID3D12Resource> m_result = nullptr;
	UINT64 m_resultSizeInBytes = 0;
	std::atomic_bool m_isDirty = true;
};

class DX12Rendering::TopLevelAccelerationStructure {
public:
	TopLevelAccelerationStructure(BottomLevelAccelerationStructure* blas);
	~TopLevelAccelerationStructure();

	void AddInstance(const dxHandle_t& index, const DirectX::XMMATRIX& transform);
	void Reset(); // Clears the acceleration structure.

	/// <summary>
	/// Updates the TLAS resource structure.
	/// </summary>
	/// <param name="device"></param>
	/// <param name="commandList"></param>
	/// <param name="scratchBuffer"></param>
	/// <param name="scratchBufferSize"></param>
	/// <returns>True if the resource buffer was updated. False otherwise.</returns>
	bool UpdateResources(ID3D12Device5* device, ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, UINT scratchBufferSize);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() { return m_result->GetGPUVirtualAddress(); }
	ID3D12Resource* GetResult() { return m_result.Get(); }
private:
	BottomLevelAccelerationStructure* m_blas;

	ComPtr<ID3D12Resource> m_result; // Top Level Acceleration Structure - Used for raytracing.
	UINT64 m_resultSize = 0;
	
	ComPtr<ID3D12Resource> m_instanceDesc;
	UINT64 m_instanceDescsSize = 0;
	UINT32 m_lastInstanceCount = 0;

	std::vector<DX12Rendering::Instance> m_instances;

	void CacluateBufferSizes(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize);
	void FillInstanceDescriptor(ID3D12Device5* device, UINT64 instanceDescsSize);
};
#endif