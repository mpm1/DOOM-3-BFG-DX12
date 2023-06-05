#ifndef __DX12_RENDERTARGET_H__
#define __DX12_RENDERTARGET_H__

#include "./dx12_resource.h"

namespace DX12Rendering {
	enum eRenderSurface
	{
		DepthStencil = 0,

		// Even though this uses forward rendering, will separate into separate resources. We may use this later to handle denoising on our raytracing.
		Diffuse,
		Specular,

		// RayTracing
		RaytraceShadowMap,
		RaytraceDiffuseMap,

		// Final Result
		RenderTarget1,
		RenderTarget2,

		Count
	};

	struct RenderSurface : public Resource
	{
		RenderSurface(const LPCWSTR name, const DXGI_FORMAT format);
		~RenderSurface();

		bool Resize(UINT width, UINT height, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, const D3D12_CLEAR_VALUE* clearValue = nullptr);

		void CreateDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE destDesc);
		void CreateRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE destDesc);

		/// <summary>
		/// Uses the last known format state to define a Resource Barrier to the new state.
		/// </summary>
		/// <param name="toTransition">The transition to. If this is the same as the last know state, no barrier is made.</param>
		/// <param name="resourceBarrier">The barrier to store the stransition information in.</params>
		/// <returns>True if a transition was created.</returns>
		bool TryTransition(const D3D12_RESOURCE_STATES toTransition, D3D12_RESOURCE_BARRIER* resourceBarrier);

	private:
		const DXGI_FORMAT m_format;
		UINT m_width;
		UINT m_height;
		D3D12_RESOURCE_STATES m_lastTransitionState;

		D3D12_CPU_DESCRIPTOR_HANDLE m_dsv;
		D3D12_CPU_DESCRIPTOR_HANDLE m_rtv;
	};

	void GenerateRenderSurfaces();
	RenderSurface* GetSurface(eRenderSurface surface);
	static RenderSurface* GetSurface(UINT surface) { return GetSurface(static_cast<eRenderSurface>(surface)); }
}

#endif