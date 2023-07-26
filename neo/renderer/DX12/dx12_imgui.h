#pragma once
#include "./imgui/imgui.h"
#include "./imgui/imgui_impl_win32.h"
#include "./imgui/imgui_impl_dx12.h"

#ifndef DEBUG_IMGUI
#define DEBUG_IMGUI
#endif

namespace DX12Rendering
{
	const ImVec4 ImGui_Color_Error = { 1, 0, 0, 1 };

	void ImGui_InitForGame(void* hwnd, ID3D12Device* device, int num_frames_in_flight, ID3D12DescriptorHeap* cbv_srv_heap);
	void ImGui_StartFrame();
	void ImGui_EndFrame(ID3D12DescriptorHeap* cbv_srv_heap, const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargets);
};