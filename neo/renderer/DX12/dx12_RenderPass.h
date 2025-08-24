#ifndef __DX12_RENDER_PASS_H__
#define __DX12_RENDER_PASS_H__

#include "./dx12_global.h"
#include "./dx12_RenderTarget.h"

#define MAX_RENDER_TARGETS 6

namespace DX12Rendering
{
	// Defines a single graphics pass (i.e. z-pass, GBuffer, transparents, emmisives). It will setup the root signature and all render targets for the pass.
	struct RenderPassBlock;
}

struct DX12Rendering::RenderPassBlock
{ // TODO: Make all passes use this block object.
	const UINT renderTargetCount;
	const std::string name;

	/// <summary>
	/// Defines a code block that will setup the current root signature and render targets. When the block is complete, we will execute the command lists and return to the generic render targets.
	/// </summary>
	/// <param name="name">Name of the block. This will show up in PIX captures.</param>
	/// <param name="commandListType">The type of command list to execute these actions on.</param>
	/// <param name="renderTargetList">An array of data specifying all of the render targets to attach.</param>
	/// <param name="renderTargetCount">The total numer of render targets to attach.</param>
	RenderPassBlock(const std::string name, const DX12Rendering::Commands::dx12_commandList_t commandListType, const DX12Rendering::eRenderSurface* renderTargetList = nullptr, const UINT renderTargetCount = 0);
	~RenderPassBlock();

	DX12Rendering::Commands::CommandManager* GetCommandManager() { return m_commandManager; }
	static RenderPassBlock* GetCurrentRenderPass();

private:
	DX12Rendering::eRenderSurface m_renderSurfaces[MAX_RENDER_TARGETS];
	DX12Rendering::Commands::CommandManager* m_commandManager;

	void UpdateRenderState(DX12Rendering::Commands::CommandList* commandList, D3D12_RESOURCE_STATES renderState);
};

#endif // __DX12_RENDER_PASS_H__