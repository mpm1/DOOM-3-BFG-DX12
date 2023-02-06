#ifndef __DX12_ACCELERATION_STRUCTURE_H__
#define __DX12_ACCELERATION_STRUCTURE_H__

#include <map>
#include <atomic>

#include "./dx12_global.h"
#include "./dx12_resource.h"

using namespace DirectX;
using namespace Microsoft::WRL;

#define DEFAULT_BLAS_SCRATCH_SIZE 3145728 // 1024 * 1024 * 3. We need to check if this is big enough.
#define DEFAULT_TLAS_SCRATCH_SIZE 3145728 

#define DEFAULT_TLAS_COUNT 512

namespace DX12Rendering 
{
	/// <summary>
	/// The base structure tree for all objects being referenced in the scene.
	/// </summary>
	class BLASManager;

	/// <summary>
	/// Container holding all TLAS objects
	/// </summary>
	class TLASManager;

	struct DX12AccelerationObject {
		D3D12_RAYTRACING_GEOMETRY_DESC vertex_buffer;
		UINT index;

		DX12AccelerationObject(D3D12_RAYTRACING_GEOMETRY_DESC desc, UINT index) :
			vertex_buffer(desc),
			index(index) {}
	};

	struct Instance {
		float				transformation[16];
		const dxHandle_t	instanceId;
		UINT				hitGroupIndex; // TODO: We will change this to point to the hitGroup in the stack that contains the normal map for the surface.
		//TODO: Add support for bone information.

		Instance(const float srcTransformation[16], dxHandle_t& id, UINT hitGroupIndex) :
			instanceId(id),
			hitGroupIndex(hitGroupIndex),
			transformation()
		{
			std::memcpy(transformation, srcTransformation, sizeof(float[16]));
		}
	};

	struct InstanceDescriptor : Resource
	{
		InstanceDescriptor(const LPCWSTR name) : Resource(name)
		{}

		void Fill(BLASManager& blasManager, UINT64 instanceDescsSize, const DX12Rendering::Instance* instances, const UINT instanceCount);

#ifdef DEBUG_IMGUI
		const void ImGuiDebug();
#endif
	private:
		UINT64 m_instanceDescsSize = 0;
		UINT32 m_lastInstanceCount = 0;
	};

	struct BottomLevelAccelerationStructure : Resource
	{
		const dxHandle_t id;
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry = {};

		BottomLevelAccelerationStructure(dxHandle_t id, const LPCWSTR name) :
			Resource(name),
			id(id),
			m_sizeInBytes(0)
		{}

		~BottomLevelAccelerationStructure()
		{
			geometry.clear();
		}

		void AddGeometry(DX12VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount);
		void AddGeometry(D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc);

		/// <summary>
		/// Builds or rebuilds the blas
		/// </summary>
		bool Generate(ScratchBuffer& scratch);

	private:
		UINT64 m_sizeInBytes;
		void CalculateBufferSize(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags);
	};

	/// <summary>
	/// The structure defining which objects will be active in the scene and which shaders will be used.
	/// </summary>
	struct TopLevelAccelerationStructure : Resource
	{
		TopLevelAccelerationStructure(const std::wstring name) :
			Resource(name.c_str()),
			m_instanceDescriptor((name + L": Descriptor").c_str())
		{}
		~TopLevelAccelerationStructure();

		/// <summary>
		/// Updates the TLAS resource structure.
		/// </summary>
		/// <param name="instances">The instances to be used in the TLAS.</param>
		/// <param name="instanceCount">The total instances in the TLAS.</param>
		/// <param name="scratchBuffer">The scratch buffer used to create the TLAS.</param>
		/// <returns>True if the resource buffer was updated. False otherwise.</returns>
		bool UpdateResources(BLASManager& blasManager, const DX12Rendering::Instance* instances, const UINT instanceCount, ScratchBuffer* scratchBuffer);

		ID3D12Resource* GetResult() { return resource.Get(); }

#ifdef DEBUG_IMGUI
		const void ImGuiDebug();
#endif
	private:
		InstanceDescriptor m_instanceDescriptor; // Used to store the information of all instnaces on the GPU
		UINT64 m_resultSize = 0;

		void CacluateBufferSizes(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize, const UINT instanceCount);
	};
}

class DX12Rendering::BLASManager {
	friend struct BottomLevelAccelerationStructure;

public:
	BLASManager();
	~BLASManager();

	BottomLevelAccelerationStructure* CreateBLAS(const dxHandle_t& key, const LPCWSTR name);
	BottomLevelAccelerationStructure* GetBLAS(const dxHandle_t& key);
	void RemoveBLAS(const dxHandle_t& key);

	void Reset();

	/// <summary>
	/// Loops through all BLAS objects and updates as needed.
	/// </summary>
	bool Generate();

#ifdef DEBUG_IMGUI
	const void ImGuiDebug();
#endif
private:
	std::map<dxHandle_t, BottomLevelAccelerationStructure> m_objectMap = {};
	ScratchBuffer m_scratchBuffer;

	DX12Rendering::Fence m_fence;
};

class DX12Rendering::TLASManager
{
public:
	TLASManager(BLASManager* blasManager);
	~TLASManager();

	bool Generate();

	void Reset();
	bool IsReady() { return m_instances.size() > 0 && m_tlas[GetCurrentFrameIndex()].fence.IsFenceCompleted(); }

	TopLevelAccelerationStructure& GetCurrent() { return m_tlas[GetCurrentFrameIndex()]; }

	void AddInstance(const dxHandle_t& id, const float transform[16]);

#ifdef DEBUG_IMGUI
	const void ImGuiDebug();
#endif
private:
	BLASManager* m_blasManager;
	std::vector<DX12Rendering::Instance> m_instances;
	TopLevelAccelerationStructure m_tlas[DX12_FRAME_COUNT];
	ScratchBuffer m_scratch;

	const bool TryGetWriteInstance(const dxHandle_t& instanceId, DX12Rendering::Instance** outInstance);
};
#endif