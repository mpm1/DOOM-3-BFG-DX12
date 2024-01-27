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

		typedef std::function<void(ID3D12GraphicsCommandList4*)> CommandFunction;
		typedef std::function<void(ID3D12CommandQueue*)> QueueFunction;

		class CommandList;

		static std::vector<CommandList> m_commandLists;

		void InitializeCommandLists();
		CommandList* GetCommandList(const dx12_commandList_t commandListType);

		void CommandListsBeginFrame();
		void CommandListsEndFrame();

		// Defines that a section of code will cycle its command list on exit.
		struct CommandListCycleBlock;
	}
}

class DX12Rendering::Commands::CommandList
{
public:
	const bool resetPerFrame;

	CommandList(D3D12_COMMAND_QUEUE_DESC* queueDesc, const bool resetPerFrame, const LPCWSTR name);
	~CommandList();

	/// <summary>
	/// Resets the command list. Returns true if we reset correctly; false otherwise.
	/// </summary>
	bool Reset();

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
	bool Execute();

	void Cycle() { Execute(); Reset(); }
	
	// Used to add command list actions to the command queue.
	void AddCommandAction(CommandFunction func);

	// Used to add a command queue action that will be fired before executing the command list.
	void AddPreExecuteQueueAction(QueueFunction func);

	// Used to add a command que action that will be fired after executing the command list.
	void AddPostExecuteQueueAction(QueueFunction func);

	bool HasRemainingActions() { return m_commandCount > 0 || m_preQueuedFunctions.size() > 0 || m_postQueuedFunctions.size() > 0;  }
	bool IsExecutable() { return m_state == OPEN; }
	
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

	ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue.Get(); }
private:
#ifdef _DEBUG
	const std::wstring m_name;
#endif
	dx12_commandListState_t m_state;
	UINT m_commandCount;
	UINT m_chunkDepth;

	std::vector<QueueFunction> m_preQueuedFunctions;
	std::vector<QueueFunction> m_postQueuedFunctions;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator[DX12_FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;

	UINT8 GetCurrentFrameIndex() { return resetPerFrame ? DX12Rendering::GetCurrentFrameIndex() : 0; }
	UINT8 GetLastFrameIndex() { return resetPerFrame ? DX12Rendering::GetLastFrameIndex() : 0; }
};

struct DX12Rendering::Commands::CommandListCycleBlock
{
	/// <summary>
	/// Signals the command cycle at the end of the block execution.
	/// </summary>
	CommandListCycleBlock(DX12Rendering::Commands::CommandList* commandList, const std::string message) :
		m_commandList(commandList)
	{
		DX12Rendering::CaptureEventStart(commandList, message);
	}

	~CommandListCycleBlock()
	{
		DX12Rendering::CaptureEventEnd(m_commandList);
		m_commandList->Cycle();
	}

private:
	DX12Rendering::Commands::CommandList* m_commandList;
};

#endif