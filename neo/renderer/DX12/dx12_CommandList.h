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
			IN_CHUNK,
			CLOSED
		};

		enum dx12_commandList_t
		{
			DIRECT = 0,
			COPY,
			COMPUTE,

			COUNT
		};

		typedef std::function<void(ID3D12GraphicsCommandList4*, ID3D12CommandQueue*)> CommandFunction;

		class CommandList;

		static std::vector<CommandList> m_commandLists;

		void InitializeCommandLists();
		CommandList* GetCommandList(const dx12_commandList_t commandListType);

		void CommandListsBeginFrame();
		void CommandListsEndFrame();

		// Defines that a section of code should be executed as a command block.
		struct CommandChunkBlock;

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

	bool SignalFence(DX12Rendering::Fence& fence) { fence.Signal(DX12Rendering::Device::GetDevice(), m_commandQueue.Get()); }

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
	
	void AddCommand(CommandFunction func);

	bool IsExecutable() { return m_commandCount > 0 && m_state == OPEN; }

	void BeginCommandChunk();
	void EndCommandChunk();
	
#pragma region Command Shortcuts
	void CommandSetPipelineState(ID3D12PipelineState* pipelineState)
	{
		AddCommand([&pipelineState](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			commandList->SetPipelineState(pipelineState);
		});
	}

	void CommandResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers)
	{
		AddCommand([&numBarriers, &barriers](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			commandList->ResourceBarrier(numBarriers, barriers);
		});
	}

	void CommandSetDescriptorHeaps(UINT numHeaps, ID3D12DescriptorHeap* heaps)
	{
		AddCommand([&numHeaps, &heaps](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			commandList->SetDescriptorHeaps(numHeaps, &heaps);
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
	UINT m_commandThreshold;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator[DX12_FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
};

struct DX12Rendering::Commands::CommandChunkBlock
{
	CommandChunkBlock(DX12Rendering::Commands::CommandList* commandList) :
		m_commandList(commandList)
	{
		m_commandList->BeginCommandChunk();
	}

	~CommandChunkBlock()
	{
		m_commandList->EndCommandChunk();
	}

private:
	DX12Rendering::Commands::CommandList* m_commandList;
};

struct DX12Rendering::Commands::CommandListCycleBlock
{
	CommandListCycleBlock(DX12Rendering::Commands::CommandList* commandList) :
		m_commandList(commandList)
	{}

	~CommandListCycleBlock()
	{
		m_commandList->Cycle();
	}

private:
	DX12Rendering::Commands::CommandList* m_commandList;
};

#endif