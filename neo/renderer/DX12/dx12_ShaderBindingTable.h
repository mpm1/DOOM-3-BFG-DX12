#ifndef __DX12_SHADER_BINDING_TABLE_H__
#define __DX12_SHADER_BINDING_TABLE_H__

#include "./dx12_global.h"
#include <any>

using namespace DirectX;

namespace DX12Rendering {
	class ShaderBindingTable;

	struct SBTEntry
	{
		const std::wstring m_entryPoint; // The name of the symbol to run.
		const std::vector<void*> m_inputData; // The name of all possible input data. I.e. Output Buffers, Textures, etc.

		SBTEntry(std::wstring entryPoint, std::vector<void*> inputData) :
			m_entryPoint(std::move(entryPoint)),
			m_inputData(std::move(inputData)) {}
	};
}

class DX12Rendering::ShaderBindingTable {
public:
	ShaderBindingTable();
	~ShaderBindingTable();

	void Reset();
	void Generate(ID3D12Resource* sbtBuffer, ID3D12StateObjectProperties* raytracingPipeline);

	void AddRayGeneratorProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData);
	void AddRayMissProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData);
	void AddRayHitGroupProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData);
	void AddCallableShaderProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData);

	UINT32 GetGeneratorEntrySize() const { return m_maxGeneratorSize; }
	UINT32 GetMissEntrySize() const { return m_maxMissSize; }
	UINT32 GetHitGroupEntrySize() const { return m_maxHitSize; }
	UINT32 GetMaxCallableShaderEntrySize() const { return m_maxShaderSize; }

	UINT32 GetGeneratorSectorSize() const { return m_maxGeneratorSize * m_generatorPrograms.size(); }
	UINT32 GetMissSectorSize() const { return m_maxMissSize * m_missPrograms.size(); }
	UINT32 GetHitGroupSectorSize() const { return m_maxHitSize * m_hitGroups.size(); }
	UINT32 GetCallableShaderSectorSize() const { return m_maxShaderSize * m_callableShaderTable.size(); }

	UINT32 CalculateTableSize();
private:
	std::vector<SBTEntry> m_generatorPrograms;
	std::vector<SBTEntry> m_missPrograms;
	std::vector<SBTEntry> m_hitGroups;
	std::vector<SBTEntry> m_callableShaderTable;

	// Each entry size is based on the max entry size for each group.
	UINT32 m_maxGeneratorSize;
	UINT32 m_maxMissSize;
	UINT32 m_maxHitSize;
	UINT32 m_maxShaderSize;

	UINT32 m_sbtSize;

	const UINT m_progIdSize = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

	UINT32 CalculateEntrySize(const std::vector<SBTEntry>& entries);
	UINT32 CopyShaderData(ID3D12StateObjectProperties* raytracingPipeline, UINT8* pData, const std::vector<SBTEntry>& shaders, const UINT32 entrySize);
};

#endif