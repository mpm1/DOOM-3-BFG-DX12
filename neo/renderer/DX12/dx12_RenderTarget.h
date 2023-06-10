#ifndef __DX12_RENDERTARGET_H__
#define __DX12_RENDERTARGET_H__

#include "./dx12_resource.h"

namespace DX12Rendering {
	typedef
		enum RENDER_SURFACE_FLAGS
	{
		RENDER_SURFACE_FLAG_NONE		= 0,
		RENDER_SURFACE_FLAG_SWAPCHAIN	= 1 << 0,
	} 	RENDER_SURFACE_FLAGS;

	DEFINE_ENUM_FLAG_OPERATORS(RENDER_SURFACE_FLAGS);

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

	const std::vector<eRenderSurface> ViewDepthStencils =
	{
		eRenderSurface::DepthStencil
	};

	const std::vector<eRenderSurface> ViewRenderTarget =
	{
		eRenderSurface::Diffuse,
		eRenderSurface::Specular,
		eRenderSurface::RaytraceDiffuseMap,
		eRenderSurface::RenderTarget1,
		eRenderSurface::RenderTarget2,
	};

	class RenderSurface : public Resource
	{
	public:
		const eRenderSurface surfaceId;

		RenderSurface(const LPCWSTR name, const DXGI_FORMAT format, const eRenderSurface surfaceId, const RENDER_SURFACE_FLAGS flags, const D3D12_CLEAR_VALUE clearValue);
		~RenderSurface();

		bool Resize(UINT width, UINT height);

		// Re-assigns the resource to the one passed in.
		bool AttachSwapchain(UINT index, IDXGISwapChain3& swapChain);

		/// <summary>
		/// Uses the last known format state to define a Resource Barrier to the new state.
		/// </summary>
		/// <param name="toTransition">The transition to. If this is the same as the last know state, no barrier is made.</param>
		/// <param name="resourceBarrier">The barrier to store the stransition information in.</params>
		/// <returns>True if a transition was created.</returns>
		bool TryTransition(const D3D12_RESOURCE_STATES toTransition, D3D12_RESOURCE_BARRIER* resourceBarrier);

		const D3D12_CPU_DESCRIPTOR_HANDLE& GetRtv() const { return m_rtv; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDsv() const { return m_dsv; }

	private:
		const DXGI_FORMAT m_format;
		const RENDER_SURFACE_FLAGS m_flags;
		UINT m_width;
		UINT m_height;
		D3D12_RESOURCE_STATES m_lastTransitionState;
		D3D12_CLEAR_VALUE m_clearValue;

		D3D12_CPU_DESCRIPTOR_HANDLE m_dsv;
		D3D12_CPU_DESCRIPTOR_HANDLE m_rtv;

		void CreateDepthStencilView();
		void CreateRenderTargetView();
		void UpdateData(UINT width, UINT height);
	};

	void GenerateRenderSurfaces();
	void DestroySurfaces();

	RenderSurface* GetSurface(const eRenderSurface surface);
	static RenderSurface* GetSurface(const UINT surface) { return GetSurface(static_cast<const eRenderSurface>(surface)); }
}

#endif