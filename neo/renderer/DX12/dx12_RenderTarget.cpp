#pragma hdrstop

#include "./dx12_RenderTarget.h"

namespace
{
	std::vector<DX12Rendering::RenderSurface> m_surfaces;
}

namespace DX12Rendering
{
	RenderSurface::RenderSurface(const LPCWSTR name, const DXGI_FORMAT format) :
		Resource(name),
		m_width(0),
		m_height(0),
		m_lastTransitionState(D3D12_RESOURCE_STATE_STREAM_OUT),
		m_format(format),
		m_dsv({}),
		m_rtv({})
	{
	}

	RenderSurface::~RenderSurface()
	{
	}

	bool RenderSurface::Resize(UINT width, UINT height, D3D12_RESOURCE_FLAGS flags, const D3D12_CLEAR_VALUE* clearValue)
	{
		assert(width > 0 && height > 0);

		bool resourceUpdated = false;

		m_width = width;
		m_height = height;

		Release();

		// Create the resource to draw raytraced shadows to.
		D3D12_RESOURCE_DESC description = {};
		description.DepthOrArraySize = 1;
		description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		description.Format = m_format; //DXGI_FORMAT_R8_UINT;
		description.Flags = flags;
		description.Width = m_width;
		description.Height = m_height;
		description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		description.MipLevels = 1;
		description.SampleDesc.Count = 1;

		if (Allocate(description, D3D12_RESOURCE_STATE_COPY_SOURCE, kDefaultHeapProps, clearValue) != nullptr)
		{
			resourceUpdated = true;
		}

		m_lastTransitionState = D3D12_RESOURCE_STATE_COPY_SOURCE;

		return resourceUpdated;
	}

	void RenderSurface::CreateDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE descDest)
	{
		assert(m_dsv.ptr == 0);

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		assert(device);

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = m_format;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;
		dsv.Flags = D3D12_DSV_FLAG_NONE;

		device->CreateDepthStencilView(resource.Get(), &dsv, descDest);

		m_dsv = descDest;
	}

	void RenderSurface::CreateRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE descDest)
	{
		assert(m_rtv.ptr == 0);

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		assert(device);

		device->CreateRenderTargetView(resource.Get(), nullptr, descDest);

		m_rtv = descDest;
	}

	bool RenderSurface::TryTransition(const D3D12_RESOURCE_STATES toTransition, D3D12_RESOURCE_BARRIER* resourceBarrier)
	{
		if (m_lastTransitionState == toTransition)
		{
			return false;
		}

		if (resourceBarrier == nullptr)
		{
			return false;
		}

		*resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), m_lastTransitionState, toTransition);
		m_lastTransitionState = toTransition;

		return true;
	}

	void GenerateRenderSurfaces()
	{
		m_surfaces.reserve(eRenderSurface::Count);
		
		m_surfaces.emplace_back(L"DepthStencil", DXGI_FORMAT_D24_UNORM_S8_UINT);

		m_surfaces.emplace_back(L"Diffuse", DXGI_FORMAT_R8G8B8A8_UNORM);
		m_surfaces.emplace_back(L"Specular", DXGI_FORMAT_R8G8B8A8_UNORM);

		m_surfaces.emplace_back(L"RaytraceShadowMap", DXGI_FORMAT_R8_UNORM);
		m_surfaces.emplace_back(L"RayTraceDiffuseMap", DXGI_FORMAT_R8G8B8A8_UNORM); // Temp for now.

		m_surfaces.emplace_back(L"RenderTarget1", DXGI_FORMAT_R8G8B8A8_UNORM);
		m_surfaces.emplace_back(L"RenderTarget2", DXGI_FORMAT_R8G8B8A8_UNORM);
	}

	RenderSurface* GetSurface(eRenderSurface surface)
	{
		assert(surface >= 0 && surface < eRenderSurface::Count);

		return &m_surfaces[surface];
	}
}