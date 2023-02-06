#pragma hdrstop

#include <algorithm>
#include "./dx12_CommandList.h"

namespace DX12Rendering {
	namespace Commands {
#pragma region Static Functions

		void InitializeCommandLists(const ID3D12Device5* device)
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
		CommandList::CommandList(ID3D12Device5* device, D3D12_COMMAND_QUEUE_DESC* queueDesc, const bool resetPerFrame, const LPCWSTR name)
			: resetPerFrame(resetPerFrame),
			m_device(device),
			m_state(UNKNOWN),
			m_commandCount(0),
			m_commandThreshold(128),
#ifdef _DEBUG
			m_name(std::wstring(name))
#endif
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

			m_state = CLOSED;
		}

		CommandList::~CommandList()
		{

		}

		void CommandList::BeginCommandChunk()
		{
			assert(m_state == OPEN);

			m_state = IN_CHUNK;
		}

		void CommandList::EndCommandChunk()
		{
			assert(m_state == IN_CHUNK);

			m_state = OPEN;
		}

		void CommandList::AddCommand(CommandFunction func)
		{
			if (func != nullptr)
			{
				assert(m_state >= OPEN && m_state <= IN_CHUNK);

				func(m_commandList.Get(), m_commandQueue.Get());

				++m_commandCount;
				if (m_commandCount >= m_commandThreshold)
				{
					Cycle();
				}
			}
		}

		bool CommandList::Execute()
		{
			if (IsExecutable())
			{
				if (!WarnIfFailed(m_commandList->Close()))
				{
					return false;
				}

				m_state = CLOSED;

				ID3D12CommandList* const ppCommandLists[] = { m_commandList.Get() };
				m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

				m_commandCount = 0;
			}

			return true;
		}

		bool CommandList::Reset()
		{
			if (m_state < CLOSED)
			{
				return false;
			}

			bool result = WarnIfFailed(m_commandList->Reset(m_commandAllocator[GetCurrentFrameIndex()].Get(), NULL));
			m_state = OPEN;

			return result;
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