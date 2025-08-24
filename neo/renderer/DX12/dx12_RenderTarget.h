#ifndef __DX12_RENDERTARGET_H__
#define __DX12_RENDERTARGET_H__

#include "./dx12_resource.h"
#include "./dx12_TextureManager.h"

namespace DX12Rendering {
	typedef
		enum RENDER_SURFACE_FLAGS
	{
		RENDER_SURFACE_FLAG_NONE				= 0,
		RENDER_SURFACE_FLAG_SWAPCHAIN			= 1 << 0,
		RENDER_SURFACE_FLAG_ALLOW_UAV			= 1 << 1
	} 	RENDER_SURFACE_FLAGS;

	DEFINE_ENUM_FLAG_OPERATORS(RENDER_SURFACE_FLAGS);

	enum eRenderSurface
	{
		DepthStencil = 0,

		// Even though this uses forward rendering, will separate into separate resources. We may use this later to handle denoising on our raytracing.
		Diffuse,
		Specular,

		// GBuffer
		Normal, // Normals in the world space. This can be used for Raytracing.
		FlatNormal, // Normals before normal mapping is applied.
		Position, // The depth in view space
		Albedo, // Albedo texture used for lighting
		SpecularColor, // Specular color used for lighting.
		Reflectivity, // The frenel reflectivity based on the normal, view angle, and material properties.
		MaterialProperties, // R = Roughness, G = metallic, B = unused, A = unused

		// RayTracing
		RaytraceShadowMask, // Each bit is a light mask
		GlobalIllumination, // The expected global illumination result for the image.

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
		eRenderSurface::Normal,
		eRenderSurface::FlatNormal,
		eRenderSurface::Position,
		eRenderSurface::Albedo,
		eRenderSurface::SpecularColor,
		eRenderSurface::Reflectivity,
		eRenderSurface::MaterialProperties,

		eRenderSurface::Diffuse,
		eRenderSurface::Specular,
		eRenderSurface::RaytraceShadowMask,
		eRenderSurface::GlobalIllumination,

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

		const D3D12_CPU_DESCRIPTOR_HANDLE& GetRtv() const { return m_rtv; }
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetDsv() const { return m_dsv; }

		const D3D12_GPU_DESCRIPTOR_HANDLE& GetGPURtv() const { return m_rtv_gpu; }

		void RenderSurface::CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE& uavHeap);

		bool CopySurfaceToTexture(DX12Rendering::TextureBuffer* texture, DX12Rendering::TextureManager* textureManager);

	private:
		const DXGI_FORMAT m_format;
		const RENDER_SURFACE_FLAGS m_flags;
		UINT m_width;
		UINT m_height;
		D3D12_CLEAR_VALUE m_clearValue;

		D3D12_CPU_DESCRIPTOR_HANDLE m_dsv;
		D3D12_CPU_DESCRIPTOR_HANDLE m_rtv;

		D3D12_GPU_DESCRIPTOR_HANDLE m_rtv_gpu;

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