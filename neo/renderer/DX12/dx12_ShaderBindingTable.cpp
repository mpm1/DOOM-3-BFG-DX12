#pragma hdrstop

#include "./dx12_ShaderBindingTable.h"

namespace DX12Rendering {
	ShaderBindingTable::ShaderBindingTable()
	{
		Reset();
	}

	ShaderBindingTable::~ShaderBindingTable()
	{
	}

	void ShaderBindingTable::Reset()
	{
		m_generatorPrograms.clear();
		m_missPrograms.clear();
		m_hitGroups.clear();
		m_callableShaderTable.clear();

		m_maxGeneratorSize = 0;
		m_maxMissSize = 0;
		m_maxHitSize = 0;
		m_maxShaderSize = 0;
		m_sbtSize = 0;
	}

	void ShaderBindingTable::AddRayGeneratorProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData)
	{
		m_generatorPrograms.emplace_back(SBTEntry(entryPoint, inputData));
	}

	void ShaderBindingTable::AddRayMissProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData)
	{
		m_missPrograms.emplace_back(SBTEntry(entryPoint, inputData));
	}

	void ShaderBindingTable::AddRayHitGroupProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData)
	{
		m_hitGroups.emplace_back(SBTEntry(entryPoint, inputData));
	}

	void ShaderBindingTable::AddCallableShaderProgram(const std::wstring& entryPoint, const std::vector<void*>& inputData)
	{
		m_callableShaderTable.emplace_back(SBTEntry(entryPoint, inputData));
	}

	UINT32 ShaderBindingTable::CalculateEntrySize(const std::vector<SBTEntry>& entries)
	{
		size_t maxArgs = 0;
		for (const SBTEntry& shader : entries)
		{
			maxArgs = std::max(maxArgs, shader.m_inputData.size());
		}

		UINT32 entrySize = m_progIdSize + (8 * static_cast<UINT32>(maxArgs));

		entrySize = DX12_ALIGN(entrySize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		return entrySize;
	}

	UINT32 ShaderBindingTable::CalculateTableSize()
	{
		m_maxGeneratorSize = CalculateEntrySize(m_generatorPrograms);
		m_maxMissSize = CalculateEntrySize(m_missPrograms);
		m_maxHitSize = CalculateEntrySize(m_hitGroups);
		m_maxShaderSize = CalculateEntrySize(m_callableShaderTable);

		UINT32 size = DX12_ALIGN(
			GetGeneratorSectorSize() +
			GetMissSectorSize() +
			GetHitGroupSectorSize() +
			GetCallableShaderSectorSize(), 256);

		return size;
	}

	// Builds the shader binding table. 
	void ShaderBindingTable::Generate(ID3D12Resource* sbtBuffer, ID3D12StateObjectProperties* raytracingPipeline)
	{
		UINT32 offset = 0;

		// Map the data to the SBT
		UINT8* pData;
		ThrowIfFailed(sbtBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

		offset = CopyShaderData(raytracingPipeline, pData, m_generatorPrograms, m_maxGeneratorSize);
		pData += offset;

		offset = CopyShaderData(raytracingPipeline, pData, m_missPrograms, m_maxMissSize);
		pData += offset;

		offset = CopyShaderData(raytracingPipeline, pData, m_hitGroups, m_maxHitSize);
		pData += offset;

		offset = CopyShaderData(raytracingPipeline, pData, m_callableShaderTable, m_maxShaderSize);

		sbtBuffer->Unmap(0, nullptr);
	}

	// Copies the shader programs and properties into the SBT.
	UINT32 ShaderBindingTable::CopyShaderData(ID3D12StateObjectProperties* raytracingPipeline, UINT8* pData, const std::vector<SBTEntry>& shaders, const UINT32 entrySize)
	{
		UINT8* pEntry = pData;
		for (const SBTEntry& shader : shaders) 
		{
			void* id = raytracingPipeline->GetShaderIdentifier(shader.m_entryPoint.c_str());
			if (!id)
			{
				FailMessage("Unknown shader entry point used for SBT.");
			}

			memcpy(pEntry, id, m_progIdSize);
			memcpy(pEntry + m_progIdSize, shader.m_inputData.data(), shader.m_inputData.size() * 8);
			pEntry += entrySize;
		}

		return static_cast<UINT32>(shaders.size() * entrySize);
	}
}