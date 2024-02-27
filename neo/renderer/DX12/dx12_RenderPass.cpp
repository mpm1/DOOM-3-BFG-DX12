#pragma hdrstop

#include "./dx12_RenderPass.h"
#include "./dx12renderer.h"

extern DX12Renderer dxRenderer;

namespace
{
	static std::vector<DX12Rendering::RenderPassBlock*> m_activeRenderPasses;
}

DX12Rendering::RenderPassBlock::RenderPassBlock(const std::string name, const DX12Rendering::Commands::dx12_commandList_t commandListType, const DX12Rendering::eRenderSurface* renderTargetList, const UINT renderTargetCount) :
	name(name),
	renderTargetCount(renderTargetList == nullptr ? 1 : renderTargetCount)
{
	assert(this->renderTargetCount <= MAX_RENDER_TARGETS && this->renderTargetCount > 0);

	m_activeRenderPasses.push_back(this);

	if (renderTargetList == nullptr)
	{
		// Use our default output.
		m_renderSurfaces[0] = dxRenderer.GetOutputSurface();
	}
	else
	{
		memcpy(m_renderSurfaces, renderTargetList, sizeof(DX12Rendering::eRenderSurface) * renderTargetCount);
	}

	m_commandManager = DX12Rendering::Commands::GetCommandManager(commandListType);

	auto commandList = m_commandManager->RequestNewCommandList();

	DX12Rendering::CaptureEventStart(commandList, name);

	UpdateRenderState(*commandList, commandListType == DX12Rendering::Commands::dx12_commandList_t::COMPUTE  ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RENDER_TARGET);

	dxRenderer.SetRenderTargets(m_renderSurfaces, this->renderTargetCount);
	dxRenderer.SetCommandListDefaults(*commandList, false);

	commandList->Close();
	m_commandManager->Execute();
}

DX12Rendering::RenderPassBlock::~RenderPassBlock()
{
	auto commandList = m_commandManager->RequestNewCommandList();

	UpdateRenderState(*commandList, D3D12_RESOURCE_STATE_COMMON);
	dxRenderer.ResetRenderTargets();
	
	DX12Rendering::CaptureEventEnd(commandList);

	for (int index = 0; index < renderTargetCount; ++index)
	{
		DX12Rendering::RenderSurface* surface = DX12Rendering::GetSurface(m_renderSurfaces[index]);

		if (surface)
		{
			commandList->AddPostFenceSignal(&surface->fence);
		}
	}

	commandList->Close();
	m_commandManager->Execute();

	m_activeRenderPasses.pop_back();
}

DX12Rendering::RenderPassBlock* DX12Rendering::RenderPassBlock::GetCurrentRenderPass()
{
	return m_activeRenderPasses.back();
}

void DX12Rendering::RenderPassBlock::UpdateRenderState(DX12Rendering::Commands::CommandList& commandList, D3D12_RESOURCE_STATES renderState)
{
	commandList.AddCommandAction([renderTargetCount = this->renderTargetCount, surfaceList = this->m_renderSurfaces, renderState](ID3D12GraphicsCommandList4* commandList)
	{
		std::vector<D3D12_RESOURCE_BARRIER> transitions;
		transitions.reserve(renderTargetCount);

		for (int index = 0; index < renderTargetCount; ++index)
		{
			DX12Rendering::RenderSurface* outputSurface = DX12Rendering::GetSurface(surfaceList[index]);

			D3D12_RESOURCE_BARRIER transition;
			if (outputSurface->TryTransition(renderState, &transition))
			{
				transitions.push_back(transition);
			}
		}

		if (transitions.size() > 0)
		{
			commandList->ResourceBarrier(transitions.size(), transitions.data());
		}
	});
}