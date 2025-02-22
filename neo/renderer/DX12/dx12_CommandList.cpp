#pragma hdrstop

#include <algorithm>
#include "./dx12_CommandList.h"

namespace DX12Rendering {
	namespace Commands {
#pragma region Static Functions

		void Initialize()
		{
			// It's important that we add the command lists in the same order as the dx12_commandList_t enumerator. 
			m_commandManagers.reserve(DX12Rendering::Commands::dx12_commandList_t::COUNT);

			// Direct Commands
			{
				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

				m_commandManagers.emplace_back(&queueDesc, Commands::DIRECT, true, L"Direct", 100);
			}

			// Copy Commands
			{
				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

				m_commandManagers.emplace_back(&queueDesc, Commands::COPY, false, L"Copy", 20);
			}

			//  Commands
			{
				D3D12_COMMAND_QUEUE_DESC queueDesc = {};
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

				m_commandManagers.emplace_back(&queueDesc, Commands::COMPUTE, true, L"Compute", 30);
			}
		}

		CommandManager* GetCommandManager(const dx12_commandList_t commandListType)
		{
			assert(commandListType >= 0 && commandListType < m_commandManagers.size());

			return &m_commandManagers[commandListType];
		}

		void BeginFrame()
		{
			std::for_each(m_commandManagers.begin(), m_commandManagers.end(), [](CommandManager& manager)
			{
				manager.BeginFrame();
			});
		}

		void EndFrame()
		{
			std::for_each(m_commandManagers.begin(), m_commandManagers.end(), [](CommandManager& manager)
			{
				manager.EndFrame();
			});
		}
#pragma endregion

#pragma region CommandManager
		CommandManager::CommandManager(D3D12_COMMAND_QUEUE_DESC* queueDesc, const dx12_commandList_t commandListType, const bool resetPerFrame, const LPCWSTR name, const UINT commandListCount)
			: resetPerFrame(resetPerFrame)
			, commandListCount(commandListCount)
			, m_fence(commandListType, std::wstring(L"CommandFence: ").append(name).c_str())
#ifdef _DEBUG
			, m_name(std::wstring(name))
#endif
		{
			assert(commandListCount > 1); // Our functions need at least two command lists to work correctly

			// Describe and create the command queue
			ID3D12Device5* device = DX12Rendering::Device::GetDevice();

			ThrowIfFailed(device->CreateCommandQueue(queueDesc, IID_PPV_ARGS(&m_commandQueue)));
			std::wstring queueName = std::wstring(name) + L" Command Queue";
			m_commandQueue->SetName(queueName.c_str());

			// Create the command allocators
			const UINT frameCount = resetPerFrame ? DX12_FRAME_COUNT : 1;
			m_commandLists.reserve(commandListCount * frameCount); // We need to keep our pointers

			for (UINT frame = 0; frame < frameCount; ++frame) {
				WCHAR nameDest[64];
				wsprintfW(nameDest, L"%s Command Allocator %d", name, frame);

				DX12Rendering::ThrowIfFailed(device->CreateCommandAllocator(queueDesc->Type, IID_PPV_ARGS(&m_commandAllocator[frame])));
				m_commandAllocator[frame]->SetName(nameDest);

				// Create the command lists
				CommandList* commandListStart = nullptr;

				for (UINT index = 0; index < commandListCount; ++index) {
					WCHAR listName[64];
					wsprintfW(listName, L"%s Command List %d:%d", name, frame, index);

					m_commandLists.emplace_back(queueDesc->Type, m_commandAllocator[frame].Get(), &m_fence, listName);

					if (index == 0)
					{
						// Store the first command list to set the last command list to cycle.
						commandListStart = &(m_commandLists.back());
					}
					else
					{
						// Set the next value of the previous command list.
						m_commandLists.at(m_commandLists.size() - 2).m_next = &(m_commandLists.back());
					}
				}

				// Set the last command list next
				m_commandLists.back().m_next = commandListStart;
			}

			m_commandListStart = &m_commandLists.at(0);
			m_commandListNext = m_commandListStart;
		}

		CommandManager::~CommandManager()
		{
			// Clear the command lists
			m_commandListStart = nullptr;
			m_commandListNext = nullptr;
			m_commandLists.clear();

			// Clear the command allocators
			const UINT frameCount = resetPerFrame ? DX12_FRAME_COUNT : 1;
			for (UINT frame = 0; frame < frameCount; ++frame) {
				m_commandAllocator[frame] = nullptr;
			}

			m_commandQueue = nullptr;
		}

		bool CommandManager::BeginFrame()
		{
			if (resetPerFrame)
			{
				Reset();
			}

			return true;
		}

		bool CommandManager::EndFrame()
		{
			if (resetPerFrame)
			{
				if (!Execute())
				{
					return false;
				}

				// Verify we have no open command lists
				CommandList* commandList = m_commandListStart;
				do
				{
					if (commandList->Close())
					{
						commandList->Clear(); // Empty any remaining actions from the command list.
					}

					commandList = commandList->m_next;
				} while (commandList != m_commandListStart);

				m_commandListStart = m_commandListNext; // Reset the list;
			} 

			return true;
		}

		bool CommandManager::Reset()
		{
			if (!Execute(true))
			{
				return false;
			}

			m_fence.WaitOnSelf();

			m_commandListStart = &m_commandLists[GetCommandListStartingPoint(GetCurrentFrameIndex())];
			m_commandListNext = m_commandListStart;

			if (!WarnIfFailed(m_commandAllocator[GetCurrentFrameIndex()]->Reset()))
			{
				return false;
			}

			return true;
		}

		UINT CommandManager::GetCommandListStartingPoint(const UINT allocatorIndex)
		{
			return allocatorIndex * commandListCount;
		}

		void CommandManager::InsertFenceWait(const DX12Rendering::Commands::FenceValue value)
		{
			CommandList* commandList = RequestNewCommandList();
			commandList->AddPreFenceWait(value);
			commandList->Close();
		}

		const DX12Rendering::Commands::FenceValue CommandManager::InsertFenceSignal()
		{
			CommandList* commandList = RequestNewCommandList();
			FenceValue value = commandList->AddPostFenceSignal();
			commandList->Close();

			return value;
		}

		CommandList* CommandManager::RequestNewCommandList()
		{
#ifdef _DEBUG
			// verify we have no open command lists
			for (CommandList* commandList = m_commandListStart; commandList != m_commandListNext; commandList = commandList->m_next)
			{
				assert(!commandList->IsOpen());
			}
#endif

			UINT attemptsRemaining = 3;

			while (attemptsRemaining > 0)
			{
				--attemptsRemaining;

				if (m_commandListNext->m_next != m_commandListStart || m_commandListStart == m_commandListNext)
				{
					CommandList* commandList = m_commandListNext;
					m_commandListNext = m_commandListNext->m_next;

					assert(commandList->IsAvailable());

					commandList->Reset();

					return commandList;
				}

				Execute(); // Run command lists to prepare for a new set.
			}

			assert(attemptsRemaining > 0);

			return nullptr;
		}

		bool CommandManager::Execute(const bool signalFence)
		{
			std::vector<CommandList*> executingCommandLists = {};
			executingCommandLists.reserve(commandListCount);
			
			for (CommandList* commandList = m_commandListStart; commandList != m_commandListNext; commandList = commandList->m_next)
			{
				assert(!commandList->IsOpen());

				if (commandList->IsExecutable())
				{
					// Add to the executing command lists for clearing
					executingCommandLists.push_back(commandList);

					// Add the pre qued functions then the pre queued transitions
					commandList->GetAllPreExecuteActions(m_queuedAction);

					// Add the command list to the queue
					QueuedAction action = {};
					action.type = dx12_queuedAction_t::COMMAND_LIST;
					action.commandList = commandList->GetCommandList();
					m_queuedAction.emplace_back(action);

					// Add the post queued functions then the post queued transitions
					commandList->GetAllPostExecuteActions(m_queuedAction);
				}
			}

			// Execute the command lists in order
			dx12_queuedAction_t lastActionType = dx12_queuedAction_t::NONE;
			std::vector<ID3D12CommandList*> commandLists = {};
			commandLists.reserve(commandListCount); // We can only execute a maximum of commandListCount commandLists.

			std::vector<D3D12_RESOURCE_BARRIER> barriers = {};

			for (auto action : m_queuedAction)
			{
				// Execute any stacking functions.
				if (action.type != lastActionType)
				{
					if (lastActionType == COMMAND_LIST)
					{
						// Execute all currently waiting commandlists
						m_commandQueue->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());
						commandLists.clear();
					}
					else if (lastActionType == RESOURCE_BARRIER)
					{
						// TODO
						assert(false); // Note yet implemented
					}
				}

				switch (action.type)
				{
				case COMMAND_LIST:
					commandLists.push_back(action.commandList);
					break;

				case RESOURCE_BARRIER:
					barriers.push_back(action.barrier);
					break;

				case QUEUED_FUNCTION:
					action.queueFunction(m_commandQueue.Get());
					break;
				}

				lastActionType = action.type;
			}

			// Execute any remaining actions
			if (commandLists.size() > 0)
			{
				m_commandQueue->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());
			}

			if (barriers.size() > 0)
			{
				// TODO
				assert(false); // Note yet implemented
			}

			// If we need to know that the fence is closed. Signal
			if (signalFence)
			{
				auto device = DX12Rendering::Device::GetDevice();
				m_fence.Signal(device, m_commandQueue.Get());
			}

			// Clean up
			m_queuedAction.clear();

			std::for_each(executingCommandLists.begin(), executingCommandLists.end(),
				[](CommandList* commandList)
			{
				commandList->Clear();
			});

			m_commandListStart = m_commandListNext;

			return true;
		}
#pragma endregion

#pragma region CommandList
		CommandList::CommandList(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* allocator, DX12Rendering::Commands::Fence* fence, const LPCWSTR name)
			: 
#ifdef _DEBUG
			m_name(std::wstring(name)),
#endif
			m_state(UNKNOWN),
			m_commandCount(0),
			m_chunkDepth(0),
			m_fence(fence)

		{
			ID3D12Device5* device = DX12Rendering::Device::GetDevice();

			//Create the command list
			m_commandAllocator = allocator;
			DX12Rendering::ThrowIfFailed(device->CreateCommandList(0, type, allocator, NULL, IID_PPV_ARGS(&m_commandList)));
			DX12Rendering::ThrowIfFailed(m_commandList->Close());
			m_commandList->SetName(std::wstring(name).c_str());

			m_state = CLOSED;
		}

		CommandList::~CommandList()
		{
			if (Close())
			{
				Clear();
			}

			m_next = nullptr;
			m_commandList = nullptr;
		}

		void CommandList::AddCommandAction(CommandFunction func)
		{
			if (func != nullptr)
			{
				assert(m_state >= OPEN);

				func(m_commandList.Get());

				++m_commandCount;
			}
		}

		void CommandList::AddPreExecuteQueueAction(QueueFunction func)
		{
			m_preQueuedFunctions.push_back(func);
		}

		void CommandList::AddPostExecuteQueueAction(QueueFunction func)
		{
			m_postQueuedFunctions.push_back(func);
		}

		void CommandList::AddPreExecuteResourceBarrier(D3D12_RESOURCE_BARRIER& barrier)
		{
			m_preQueuedResourceBarriers.push_back(barrier);
		}

		void CommandList::AddPostExecuteResourceBarrier(D3D12_RESOURCE_BARRIER& barrier)
		{
			m_postQueuedResourceBarriers.push_back(barrier);
		}

		void CommandList::GetAllPreExecuteActions(std::vector<QueuedAction>& actions)
		{
			std::for_each(m_preQueuedFunctions.begin(), m_preQueuedFunctions.end(),
				[&actions](QueueFunction function)
			{
				QueuedAction action = {};
				action.type = dx12_queuedAction_t::QUEUED_FUNCTION;
				action.queueFunction = function;

				actions.emplace_back(action);
			});

			std::for_each(m_preQueuedResourceBarriers.begin(), m_preQueuedResourceBarriers.end(),
				[&actions](D3D12_RESOURCE_BARRIER barrier)
			{
				QueuedAction action = {};
				action.type = dx12_queuedAction_t::RESOURCE_BARRIER;
				action.barrier = barrier;

				actions.emplace_back(action);
			});
		}

		void CommandList::GetAllPostExecuteActions(std::vector<QueuedAction>& actions)
		{
			std::for_each(m_postQueuedFunctions.begin(), m_postQueuedFunctions.end(),
				[&actions](QueueFunction function)
			{
				QueuedAction action = {};
				action.type = dx12_queuedAction_t::QUEUED_FUNCTION;
				action.queueFunction = function;

				actions.emplace_back(action);
			});

			std::for_each(m_postQueuedResourceBarriers.begin(), m_postQueuedResourceBarriers.end(),
				[&actions](D3D12_RESOURCE_BARRIER barrier)
			{
				QueuedAction action = {};
				action.type = dx12_queuedAction_t::RESOURCE_BARRIER;
				action.barrier = barrier;

				actions.emplace_back(action);
			});
		}

		bool CommandList::Reset()
		{
			if (!Clear())
			{
				return false;
			}

			bool result = WarnIfFailed(m_commandList->Reset(m_commandAllocator, NULL));
			m_state = OPEN;

			return result;
		}

		bool CommandList::Close()
		{
			if (m_state == CLOSED)
			{
				return true;
			}
			else if (m_state != OPEN)
			{
				// If we are not closed, and not open, we have an error and should fail.
				return false;
			}

			if (!WarnIfFailed(m_commandList->Close()))
			{
				return false;
			}

			m_state = CLOSED;

			return true;
		}

		bool CommandList::Clear()
		{
			if (m_state == CLOSED)
			{
				m_preQueuedFunctions.clear();
				m_preQueuedResourceBarriers.clear();

				m_postQueuedFunctions.clear();
				m_postQueuedResourceBarriers.clear();

				m_commandCount = 0;

				return true;
			}

			return false;
		}

		bool CommandList::HasRemainingActions() const { 
			return m_commandCount > 0 || m_preQueuedFunctions.size() > 0 || m_postQueuedFunctions.size() > 0;
		}

		bool CommandList::IsExecutable() const { return m_state == CLOSED && HasRemainingActions(); }
		bool CommandList::IsAvailable() const { return m_state == CLOSED && !HasRemainingActions(); }
#pragma endregion
	}
}