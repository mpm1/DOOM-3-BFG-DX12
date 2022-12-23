#pragma hdrstop

#include <algorithm>
#include "./dx12_CommandList.h"

namespace DX12Rendering {
	namespace Commands {
#pragma region Static Functions

		void InitializeCommandLists(ID3D12Device5* device)
		{
			// It's important that we add the command lists in the same order as the dx12_commandList_t enumerator. 
			m_commandLists.reserve(DX12Rendering::Commands::dx12_commandList_t::COUNT);

			// Direct Commands
			{
				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

				m_commandLists.emplace_back(device, &queueDesc, true, L"Direct");
			}

			// Copy Commands
			{
				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

				m_commandLists.emplace_back(device, &queueDesc, true, L"Copy");
			}

			//  Commands
			{
				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

				m_commandLists.emplace_back(device, &queueDesc, false, L"Compute");
			}
		}

		CommandList* GetCommandList(const dx12_commandList_t commandListType)
		{
			assert(commandListType >= 0 && commandListType < m_commandLists.size());

			return &m_commandLists[commandListType];
		}

		void CommandListsBeginFrame()
		{
			std::for_each(m_commandLists.begin(), m_commandLists.end(), [](CommandList& commandList)
			{
				if (commandList.resetPerFrame)
				{
					commandList.BeginFrame();
				}
			});
		}

		void CommandListsEndFrame()
		{
			std::for_each(m_commandLists.begin(), m_commandLists.end(), [](CommandList& commandList)
			{
				if (commandList.resetPerFrame)
				{
					commandList.EndFrame();
				}
			});
		}
#pragma endregion


#pragma region CommandList
		CommandList::CommandList(ID3D12Device5* device, D3D12_COMMAND_QUEUE_DESC* queueDesc, const bool resetPerFrame, LPCWSTR name)
			: resetPerFrame(resetPerFrame),
			m_device(device),
			m_isDirty(false)
		{
			// Describe and create the command queue
			ThrowIfFailed(device->CreateCommandQueue(queueDesc, IID_PPV_ARGS(&m_commandQueue)));
			std::wstring queueName = std::wstring(name) + L" Command Queue";
			m_commandQueue->SetName(queueName.c_str());

			// Create the command allocators
			const UINT frameCount = resetPerFrame ? DX12_FRAME_COUNT : 1;
			for (int frame = 0; frame < frameCount; ++frame) {
				WCHAR nameDest[64];
				wsprintfW(nameDest, L"%s Command Allocator %d", name, frame);

				DX12Rendering::ThrowIfFailed(m_device->CreateCommandAllocator(queueDesc->Type, IID_PPV_ARGS(&m_commandAllocator[frame])));
				m_commandAllocator[frame]->SetName(nameDest);
			}

			//Create the command list
			DX12Rendering::ThrowIfFailed(device->CreateCommandList(0, queueDesc->Type, m_commandAllocator[0].Get(), NULL, IID_PPV_ARGS(&m_commandList)));
			DX12Rendering::ThrowIfFailed(m_commandList->Close());
			std::wstring commandListName = std::wstring(name) + L" Command List";
			m_commandList->SetName(commandListName.c_str());
		}

		CommandList::~CommandList()
		{

		}

		bool CommandList::Execute()
		{
			if (!WarnIfFailed(m_commandList->Close()))
			{
				return false;
			}

			if (m_isDirty)
			{
				ID3D12CommandList* const ppCommandLists[] = { m_commandList.Get() };
				m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

				m_isDirty = false;
			}

			return true;
		}

		bool CommandList::Reset()
		{
			if (m_isDirty)
			{
				return false;
			}

			return WarnIfFailed(m_commandList->Reset(m_commandAllocator[GetCurrentFrameIndex()].Get(), NULL));
		}

		bool CommandList::BeginFrame()
		{
			if (!Reset())
			{
				return false;
			}

			if (!WarnIfFailed(m_commandAllocator[GetLastFrameIndex()]->Reset()))
			{
				return false;
			}

			return true;
		}

		bool CommandList::EndFrame()
		{
			return Execute();
		}
#pragma endregion
	}
}