#ifndef __DX12_COMMAND_LIST_H__
#define __DX12_COMMAND_LIST_H__

#include <functional>

#include "./dx12_global.h"

using namespace DirectX;
using namespace Microsoft::WRL;

namespace DX12Rendering
{
	namespace Commands
	{
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

		void InitializeCommandLists(ID3D12Device5* device);
		CommandList* GetCommandList(const dx12_commandList_t commandListType);

		void CommandListsBeginFrame();
		void CommandListsEndFrame();
	}
}

class DX12Rendering::Commands::CommandList
{
public:
	const bool resetPerFrame;

	CommandList(ID3D12Device5* device, D3D12_COMMAND_QUEUE_DESC* queueDesc, const bool resetPerFrame, LPCWSTR name);
	~CommandList();

	bool SignalFence(DX12Rendering::Fence& fence) { fence.Signal(m_device, m_commandQueue.Get()); }

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

	void AddCommand(CommandFunction func)
	{
		if (func != nullptr)
		{
			m_isDirty = true;

			func(m_commandList.Get(), m_commandQueue.Get());
		}
	}

	ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue.Get(); }
private:
	bool m_isDirty;

	ID3D12Device5* m_device;

	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator[DX12_FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
};

#endif