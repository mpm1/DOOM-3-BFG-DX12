#ifndef __DX12_COMMAND_LIST_H__
#define __DX12_COMMAND_LIST_H__

#include <functional>

#include "./dx12_global.h"
#include "./dx12_DeviceManager.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering
{
	namespace Commands
	{
		typedef std::function<void(ID3D12GraphicsCommandList4*)> CommandFunction;
		typedef std::function<void(ID3D12CommandQueue*)> QueueFunction;

		enum dx12_commandListState_t
		{
			UNKNOWN = 0,
			OPEN,
			CLOSED
		};

		enum dx12_commandList_t
		{
			DIRECT = 0,
			COPY,
			COMPUTE,

			COUNT
		};

		enum dx12_queuedAction_t
		{
			NONE = 0,
			COMMAND_LIST,
			RESOURCE_BARRIER,
			QUEUED_FUNCTION
		};

		struct QueuedAction
		{
			dx12_queuedAction_t type;

			// If the type is COMMAND_LIST
			ID3D12CommandList* commandList;

			// If the type is RESOURCE_BARRIER
			D3D12_RESOURCE_BARRIER barrier;

			// If the type is QUEUED_FUNCTION
			QueueFunction queueFunction;
		};

		class CommandManager;
		class CommandList;

		static std::vector<CommandManager> m_commandManagers;

		void Initialize();

		CommandManager* GetCommandManager(const dx12_commandList_t commandListType);

		void BeginFrame();
		void EndFrame();

		// Defines that a section of code will execute its command manager on exit
		struct CommandManagerCycleBlock;

		// Defines that a section of code will cycle its command list on exit.
		struct CommandListCycleBlock;
	}
}

/// <summary>
/// Used to handle all command actions for a queue (Direct, Compute, or Copy).
/// This will control CommandList creation and execution.
/// </summary>
class DX12Rendering::Commands::CommandManager
{
public:
	const bool resetPerFrame;
	const UINT commandListCount;

	CommandManager(D3D12_COMMAND_QUEUE_DESC* queueDesc, const bool resetPerFrame, const LPCWSTR name, const UINT commandListCount);
	~CommandManager();

	/// <summary>
	/// Closes the previous command list, clears the command allocator, and increments the frame index. Returns false if we could not reset the command list or reset the command allocator.
	/// </summary>
	bool BeginFrame();

	/// <summary>
	/// Commits any remaining command list entries.
	/// </summary>
	bool EndFrame();

	/// <summary>
	/// Executes all loaded commands onto the command queue.
	/// </summary>
	bool Execute(const bool signalFence = false);

	/// <summary>
	/// Resets the command allocator and all actions.
	/// </summary>
	bool Reset();

	/// <summary>
	/// Requests a command list from the manager. If there are no free command lists, the system will execute all available lists and try to return the next available one.
	/// </summary>
	/// <returns>A valid command list that's ready to be used.</returns>
	CommandList* RequestNewCommandList();

	ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue.Get(); }

private:
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator[DX12_FRAME_COUNT];

	std::vector<DX12Rendering::Commands::CommandList> m_commandLists; // All command lists available
	DX12Rendering::Commands::CommandList* m_commandListStart; // Current commandList starting point for execution
	DX12Rendering::Commands::CommandList* m_commandListNext; // Next available command list
	std::vector<QueuedAction> m_queuedAction;

	DX12Rendering::Fence m_fence;

#ifdef _DEBUG
	const std::wstring m_name;
#endif

	UINT8 GetCurrentFrameIndex() { return resetPerFrame ? DX12Rendering::GetCurrentFrameIndex() : 0; }
	UINT8 GetLastFrameIndex() { return resetPerFrame ? DX12Rendering::GetLastFrameIndex() : 0; }

	UINT GetCommandListStartingPoint(const UINT allocatorIndex);
};

class DX12Rendering::Commands::CommandList
{
	friend class CommandManager;

public:
	CommandList(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* allocator, const LPCWSTR name);
	~CommandList();

	/// <summary>
	/// Resets the command list. Returns true if we reset correctly; false otherwise.
	/// </summary>
	bool Reset();

	/// <summary>
	/// Close the command list so that it's ready for execution or should be dropped.
	/// </summary>
	/// <returns>True if we could close the command list or if the command list is already closed. False otherwise.</returns>
	bool Close();

	/// <summary>
	/// Clears all actions from a closed command list.
	/// </summary>
	/// <returns>True if the command list is closed all actions can be cleared. False otherwise.</returns>
	bool Clear();
		
	// Used to add command list actions to the command queue.
	void AddCommandAction(CommandFunction func);

	// Used to add a command queue action that will be fired before executing the command list.
	void AddPreExecuteQueueAction(QueueFunction func);

	// Adds a resource barrier before the execution of a command list.
	void AddPreExecuteResourceBarrier(D3D12_RESOURCE_BARRIER& barrier);

	// Used to add a command que action that will be fired after executing the command list.
	void AddPostExecuteQueueAction(QueueFunction func);

	// Adds a resource barrier after the execution of a command list. 
	void AddPostExecuteResourceBarrier(D3D12_RESOURCE_BARRIER& barrier);

	void GetAllPreExecuteActions(std::vector<QueuedAction>& actionOutput);
	void GetAllPostExecuteActions(std::vector<QueuedAction>& actionOutput);

	bool HasRemainingActions() const;
	bool IsExecutable() const;
	bool IsAvailable() const;
	bool IsOpen() const { return m_state == OPEN; }

	ID3D12GraphicsCommandList4* GetCommandList(){ return m_commandList.Get(); }
	
#pragma region Command Shortcuts
	void CommandSetPipelineState(ID3D12PipelineState* pipelineState)
	{
		AddCommandAction([&pipelineState](ID3D12GraphicsCommandList4* commandList)
		{
			commandList->SetPipelineState(pipelineState);
		});
	}

	void CommandResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers)
	{
		AddCommandAction([&numBarriers, &barriers](ID3D12GraphicsCommandList4* commandList)
		{
			commandList->ResourceBarrier(numBarriers, barriers);
		});
	}

	void CommandSetDescriptorHeaps(UINT numHeaps, ID3D12DescriptorHeap* heaps)
	{
		AddCommandAction([&numHeaps, &heaps](ID3D12GraphicsCommandList4* commandList)
		{
			commandList->SetDescriptorHeaps(numHeaps, &heaps);
		});
	}

	void AddPreFenceWait(DX12Rendering::Fence* fence)
	{
		AddPreExecuteQueueAction([fence](ID3D12CommandQueue* commandQueue)
		{
			fence->GPUWait(commandQueue);
		});
	}

	void AddPostFenceSignal(DX12Rendering::Fence* fence)
	{
		AddPostExecuteQueueAction([fence](ID3D12CommandQueue* commandQueue)
		{
			fence->Signal(DX12Rendering::Device::GetDevice(), commandQueue);
		});
	}

	void ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle, const float rgbaColor[4])
	{
		AddCommandAction([renderTargetHandle, rgbaColor](ID3D12GraphicsCommandList4* commandList)
		{
			commandList->ClearRenderTargetView(renderTargetHandle, rgbaColor, 0, nullptr);
		});
	}
#pragma endregion

private:
#ifdef _DEBUG
	const std::wstring m_name;
#endif
	dx12_commandListState_t m_state;
	UINT m_commandCount;
	UINT m_chunkDepth;

	std::vector<QueueFunction> m_preQueuedFunctions;
	std::vector<D3D12_RESOURCE_BARRIER> m_preQueuedResourceBarriers;

	std::vector<QueueFunction> m_postQueuedFunctions;
	std::vector<D3D12_RESOURCE_BARRIER> m_postQueuedResourceBarriers;

	ComPtr<ID3D12GraphicsCommandList4> m_commandList;

	ID3D12CommandAllocator* m_commandAllocator;

	CommandList* m_next;
};

struct DX12Rendering::Commands::CommandManagerCycleBlock
{
	/// <summary>
	/// Signals the command cycle at the end of the block execution.
	/// </summary>
	CommandManagerCycleBlock(DX12Rendering::Commands::CommandManager* commandManager, const std::string message) :
		m_commandManager(commandManager)
	{
		// Make sure we're starting fresh
		assert(commandManager != nullptr);

		auto commandList = commandManager->RequestNewCommandList();

		DX12Rendering::CaptureEventStart(commandList, message);

		commandList->Close();
	}

	~CommandManagerCycleBlock()
	{
		auto commandList = m_commandManager->RequestNewCommandList();

		DX12Rendering::CaptureEventEnd(commandList);

		commandList->Close();
		m_commandManager->Execute();
	}

private:
	DX12Rendering::Commands::CommandManager* m_commandManager;
};

struct DX12Rendering::Commands::CommandListCycleBlock
{
	/// <summary>
	/// Signals the command cycle at the end of the block execution.
	/// </summary>
	CommandListCycleBlock(DX12Rendering::Commands::CommandList* commandList, const std::string message) :
		m_commandList(commandList)
	{
		// Make sure we're starting fresh
		assert(commandList != nullptr && commandList->IsOpen());

		DX12Rendering::CaptureEventStart(commandList, message);
	}

	~CommandListCycleBlock()
	{
		DX12Rendering::CaptureEventEnd(m_commandList);
		m_commandList->Close();
	}

private:
	DX12Rendering::Commands::CommandList* m_commandList;
};

#endif