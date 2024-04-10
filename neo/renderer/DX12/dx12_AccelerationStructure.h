#ifndef __DX12_ACCELERATION_STRUCTURE_H__
#define __DX12_ACCELERATION_STRUCTURE_H__

#include <map>
#include <atomic>

#include "./dx12_global.h"
#include "./dx12_resource.h"
#include "./dx12_Geometry.h"

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

	typedef
		enum ACCELERATION_INSTANCE_TYPE
	{
		INSTANCE_TYPE_NONE = 0,
		INSTANCE_TYPE_STATIC = 1 << 0,
		INSTANCE_TYPE_DYNAMIC = 1 << 1
	} 	ACCELERATION_INSTANCE_TYPE;

	typedef
		enum ACCELLERATION_INSTANCE_MASK
	{
		INSTANCE_MASK_NONE = 0,
		INSTANCE_MASK_CAST_SHADOW = 1 << 0,
		INSTANCE_MASK_SKYBOX = 1 << 1

	} ACCELLERATION_INSTANCE_MASK;


	DEFINE_ENUM_FLAG_OPERATORS(ACCELERATION_INSTANCE_TYPE);

	struct RaytracingGeometryArgument {
		dxHandle_t meshIndex; // If we use multiple meshes, this is the index on the top model.
		
		dxHandle_t vertexHandle;
		UINT vertCounts;
		
		dxHandle_t indexHandle;
		UINT indexCounts;

		dxHandle_t jointsHandle;
	};

	struct DX12AccelerationObject {
		D3D12_RAYTRACING_GEOMETRY_DESC vertex_buffer;
		UINT index;

		DX12AccelerationObject(D3D12_RAYTRACING_GEOMETRY_DESC desc, UINT index) :
			vertex_buffer(desc),
			index(index) {}
	};

	struct Instance {
		float				transformation[3][4];
		const dxHandle_t	instanceId;
		dxHandle_t			blasId;
		UINT				hitGroupIndex; // TODO: We will change this to point to the hitGroup in the stack that contains the normal map for the surface.
		UINT				mask; // ACCELLERATION_INSTANCE_MASK
										   //TODO: Add support for bone information.

		Instance(const float srcTransformation[16], const dxHandle_t& id, const dxHandle_t& blasId, UINT hitGroupIndex, ACCELLERATION_INSTANCE_MASK mask) :
			instanceId(id),
			blasId(blasId),
			hitGroupIndex(hitGroupIndex),
			mask(mask),
			transformation{}
		{
			std::memcpy(transformation, srcTransformation, sizeof(float[3][4]));
		}
	};

	struct InstanceDescriptor : public Resource
	{
		InstanceDescriptor(const LPCWSTR name) : Resource(name)
		{}

		UINT Fill(UINT64 instanceDescsSize, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescriptors);

#ifdef DEBUG_IMGUI
		const void ImGuiDebug();
#endif
	private:
		UINT64 m_instanceDescsSize = 0;
		UINT32 m_lastInstanceCount = 0;
	};

	struct BottomLevelAccelerationStructure : public Resource
	{
		const dxHandle_t id;
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry = {};
		std::vector<dxHandle_t> joints = {};
		const bool m_isStatic;
		bool isBuilt; // We may need to build this structure later.

		BottomLevelAccelerationStructure(const dxHandle_t id, bool isStatic, const LPCWSTR name) :
			Resource(name),
			id(id),
			m_sizeInBytes(0),
			m_isStatic(isStatic),
			isBuilt(false)
		{}

		~BottomLevelAccelerationStructure()
		{
			geometry.clear();
		}

		void AddGeometry(DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffsetBytes, UINT vertexCount, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount, dxHandle_t jointsHandle);
		void AddGeometry(D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc);

		/// <summary>
		/// Builds or rebuilds the blas
		/// </summary>
		bool Generate(ScratchBuffer& scratch);

	private:
		UINT64 m_sizeInBytes;

		void CalculateBufferSize(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* desc);
	};

	/// <summary>
	/// The structure defining which objects will be active in the scene and which shaders will be used.
	/// </summary>
	struct TopLevelAccelerationStructure : public Resource
	{
		TopLevelAccelerationStructure(const std::wstring name) :
			Resource(std::move(name.c_str())),
			m_instanceDescriptor(InstanceDescriptor((name + L": Descriptor").c_str()))
		{}
		~TopLevelAccelerationStructure();

		/// <summary>
		/// Updates the TLAS resource structure.
		/// </summary>
		/// <param name="instances">The instances to be used in the TLAS.</param>
		/// <param name="instanceCount">The total instances in the TLAS.</param>
		/// <param name="scratchBuffer">The scratch buffer used to create the TLAS.</param>
		/// <returns>True if the resource buffer was updated. False otherwise.</returns>
		bool UpdateResources(BLASManager& blasManager, const std::vector<DX12Rendering::Instance>& staticInstances, const std::vector<DX12Rendering::Instance>& dynamicInstances, ScratchBuffer* scratchBuffer);

		ID3D12Resource* GetResult() { return resource.Get(); }

#ifdef DEBUG_IMGUI
		const void ImGuiDebug();
#endif
	private:
		InstanceDescriptor m_instanceDescriptor; // Used to store the information of all instnaces on the GPU
		UINT64 m_resultSize = 0;

		void CacluateBufferSizes(ID3D12Device5* device, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* instanceDescsSize, const UINT instanceCount, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* description);
	};
}

class DX12Rendering::BLASManager {
	friend struct BottomLevelAccelerationStructure;

public:
	BLASManager();
	~BLASManager();

	BottomLevelAccelerationStructure* CreateBLAS(const dxHandle_t& key, const bool isStatic, const bool isBuilt, const LPCWSTR name);
	BottomLevelAccelerationStructure* GetBLAS(const dxHandle_t& key);
	void RemoveBLAS(const dxHandle_t& key);

	void Reset();

	/// <summary>
	/// Loops through all BLAS objects and updates as needed.
	/// </summary>
	UINT Generate();

#ifdef DEBUG_IMGUI
	const void ImGuiDebug();
#endif
private:
	const UINT m_blasPerFrame = 100; // The maximum available BLAS objects to be processed per frame.
	UINT m_blasIndex; // Used to tell the next BLAS index to process each frame.
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

	void Reset(const ACCELERATION_INSTANCE_TYPE typesMask);
	const bool IsReady() noexcept { return GetCurrent().Exists(); }
	const void WaitForFence() { return GetCurrent().fence.Wait(); }

	TopLevelAccelerationStructure& GetCurrent() { return m_tlas[GetCurrentFrameIndex()]; }

	void AddInstance(const dxHandle_t& entityId, const dxHandle_t& blasId, const float transform[16], const ACCELERATION_INSTANCE_TYPE instanceTypes, const ACCELLERATION_INSTANCE_MASK instanceMask);

	const bool IsDirty(){ return m_isDirty; }
	void MarkDirty() { m_isDirty = true; }

#ifdef DEBUG_IMGUI
	const void ImGuiDebug();
#endif
private:
	BLASManager* m_blasManager;
	DX12Rendering::dx12_lock m_instanceLock;
	std::vector<DX12Rendering::Instance> m_staticInstances;
	std::vector<DX12Rendering::Instance> m_dynamicInstances;
	TopLevelAccelerationStructure m_tlas[DX12_FRAME_COUNT];
	ScratchBuffer m_scratch;
	bool m_isDirty;

	const bool TryGetWriteInstance(const dxHandle_t& instanceId, const ACCELERATION_INSTANCE_TYPE typesMask, DX12Rendering::Instance** outInstance);
};
#endif