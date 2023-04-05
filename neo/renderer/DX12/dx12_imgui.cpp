#pragma hdrstop

#include "dx12_imgui.h"
#include "./dx12_CommandList.h"

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
		ImGui_ImplWin32_NewFrame();
		ImGui_ImplDX12_NewFrame();
		ImGui::NewFrame();
	}

	void ImGui_EndFrame(ID3D12DescriptorHeap* cbv_srv_heap)
	{
		ImGui::Render();

		{
			auto commandList = Commands::GetCommandList(Commands::DIRECT);
			Commands::CommandListCycleBlock cycleBlock(commandList, "ImGui");

			commandList->AddCommandAction([&cbv_srv_heap](ID3D12GraphicsCommandList4* commandList)
			{
				commandList->SetDescriptorHeaps(1, &cbv_srv_heap);
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
			});
		}

		ImGui::EndFrame();
	}
}