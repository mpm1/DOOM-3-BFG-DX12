#pragma hdrstop

#include "dx12_imgui.h"
#include "./dx12_CommandList.h"
#include "./dx12_RenderTarget.h"

namespace DX12Rendering
{
	void ImGui_InitForGame(void* hwnd, ID3D12Device* device, int num_frames_in_flight, ID3D12DescriptorHeap* cbv_srv_heap)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO(); (void)io;

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX12_Init(device, num_frames_in_flight,
			DXGI_FORMAT_R8G8B8A8_UNORM, cbv_srv_heap,
			cbv_srv_heap->GetCPUDescriptorHandleForHeapStart(),
			cbv_srv_heap->GetGPUDescriptorHandleForHeapStart());
	}

	void ImGui_StartFrame()
	{
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void ImGui_EndFrame(ID3D12DescriptorHeap* cbv_srv_heap, const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargets)
	{
		ImGui::Render();
		ImGui::EndFrame();

		{
			auto commandManager = Commands::GetCommandManager(Commands::DIRECT);
			Commands::CommandManagerCycleBlock cycleBlock(commandManager, "ImGui");

			auto commandList = commandManager->RequestNewCommandList();
			commandList->AddCommandAction([cbv_srv_heap, renderTargets](ID3D12GraphicsCommandList4* commandList)
			{
				{
					DX12Rendering::RenderSurface* surface = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1);

					D3D12_RESOURCE_BARRIER transition;
					if (surface->TryTransition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &transition))
					{
						commandList->ResourceBarrier(1, &transition);
					}
				}

				commandList->SetDescriptorHeaps(1, &cbv_srv_heap);
				commandList->OMSetRenderTargets(1, renderTargets, FALSE, nullptr);
				
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
			});
			commandList->Close();
		}
	}
}