#pragma hdrstop

#include "./dx12_RenderTarget.h"
#include <algorithm>


namespace
{
	std::vector<DX12Rendering::RenderSurface> m_surfaces;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
}

namespace DX12Rendering
{
	RenderSurface::RenderSurface(const LPCWSTR name, const DXGI_FORMAT format, const eRenderSurface surfaceId, const RENDER_SURFACE_FLAGS flags, const D3D12_CLEAR_VALUE clearValue) :
		Resource(name),
		surfaceId(surfaceId),
		m_width(0),
		m_height(0),
		m_format(format),
		m_clearValue(clearValue),
		m_flags(flags),
		m_dsv({MAXSIZE_T}),
		m_rtv({MAXSIZE_T}),
		m_rtv_gpu({MAXSIZE_T})
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		
		// Create the RTV handle index
		{
			auto index = std::find(ViewRenderTarget.cbegin(), ViewRenderTarget.cend(), surfaceId);
			if (index != ViewRenderTarget.cend())
			{
				const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
				const UINT offset = index - ViewRenderTarget.cbegin();

				const CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), offset, descriptorSize);
				m_rtv = handle;

				const CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_rtvHeap->GetGPUDescriptorHandleForHeapStart(), offset, descriptorSize);
				m_rtv_gpu = gpuHandle;
			}
		}

		// Create the dsv handle index
		{
			auto index = std::find(ViewDepthStencils.cbegin(), ViewDepthStencils.cend(), surfaceId);
			if (index != ViewDepthStencils.cend())
			{
				const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
				const UINT offset = index - ViewDepthStencils.cbegin();

				const CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), offset, descriptorSize);
				m_dsv = handle;
			}
		}
	}

	RenderSurface::~RenderSurface()
	{
	}

	bool RenderSurface::AttachSwapchain(UINT index, IDXGISwapChain3& swapChain)
	{
		if (FAILED(swapChain.GetBuffer(index, IID_PPV_ARGS(&resource))))
		{
			return false;
		}

		// Hack to set ourselves in the appropriate resource state.
		state = Ready;

		D3D12_RESOURCE_BARRIER transition = {};
		TryTransition(D3D12_RESOURCE_STATE_COPY_SOURCE, &transition);

		return true;
	}

	void RenderSurface::UpdateData(UINT width, UINT height)
	{
		m_width = width;
		m_height = height;

		CreateDepthStencilView();
		CreateRenderTargetView();
	}

	bool RenderSurface::Resize(UINT width, UINT height)
	{
		if ((m_flags & RENDER_SURFACE_FLAG_SWAPCHAIN) != 0)
		{
			// The swapchain will update the buffer, so we just want to update our metadata.
			UpdateData(width, height);
			return true;
		}

		if (width == m_width && height == m_height)
		{
			return true;
		}

		bool resourceUpdated = false;

		Release();

		// Create the resource to draw raytraced shadows to.
		D3D12_RESOURCE_DESC description = {};
		description.DepthOrArraySize = 1;
		description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		description.Format = m_format; //DXGI_FORMAT_R8_UINT;
		description.Flags = D3D12_RESOURCE_FLAG_NONE;
		description.Width = width;
		description.Height = height;
		description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		description.MipLevels = 1;
		description.SampleDesc.Count = 1;

		if (m_rtv.ptr != MAXSIZE_T)
		{
			description.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}

		if (m_dsv.ptr != MAXSIZE_T)
		{
			description.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}

		if ((m_flags & RENDER_SURFACE_FLAG_ALLOW_UAV) != 0)
		{
			description.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		if (Allocate(description, D3D12_RESOURCE_STATE_COPY_SOURCE, kDefaultHeapProps, m_clearValue.Format == DXGI_FORMAT_UNKNOWN  ? nullptr : &m_clearValue) != nullptr)
		{
			resourceUpdated = true;

			UpdateData(width, height);
		}

		return resourceUpdated;
	}

	void RenderSurface::CreateDepthStencilView()
	{
		if (m_dsv.ptr == MAXSIZE_T)
		{
			return;
		}

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		assert(device);

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = m_format;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;
		dsv.Flags = D3D12_DSV_FLAG_NONE;

		device->CreateDepthStencilView(resource.Get(), &dsv, m_dsv);
	}

	void RenderSurface::CreateRenderTargetView()
	{
		if (m_rtv.ptr == MAXSIZE_T)
		{
			return;
		}

		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		assert(device);

		device->CreateRenderTargetView(resource.Get(), nullptr, m_rtv);
	}

	void RenderSurface::CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE& uavHeap)
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();

		assert(device);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

		device->CreateUnorderedAccessView(resource.Get(), nullptr, &uavDesc, uavHeap);
	}

	bool RenderSurface::CopySurfaceToTexture(DX12Rendering::TextureBuffer* texture, DX12Rendering::TextureManager* textureManager)
	{
		if (texture == nullptr)
		{
			return false;
		}

		// TODO: Put these as inputs
		UINT sx = 0;
		UINT sy = 0;
		UINT rx = 0;
		UINT ry = 0;
		UINT width = this->m_width;
		UINT height = this->m_height;

		auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY);
		DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "RenderSurface::CopySurfaceToTexture");

		textureManager->StartTextureWrite(texture);

		auto commandList = commandManager->RequestNewCommandList();
		
		textureManager->SetTextureState(texture, D3D12_RESOURCE_STATE_COPY_DEST, commandList);

		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			D3D12_RESOURCE_STATES previousState = D3D12_RESOURCE_STATE_RENDER_TARGET;

			D3D12_RESOURCE_BARRIER transition = {};
			if (this->TryTransition(D3D12_RESOURCE_STATE_COPY_SOURCE, &transition))
			{
				previousState = transition.Transition.StateBefore;
				commandList->ResourceBarrier(1, &transition);
			}

			const CD3DX12_TEXTURE_COPY_LOCATION dst(texture->resource.Get());
			const CD3DX12_TEXTURE_COPY_LOCATION src(resource.Get());
			const CD3DX12_BOX srcBox(sx, sy, sx + width, sy + height);
			commandList->CopyTextureRegion(&dst, rx, ry, 0, &src, &srcBox);

			if (TryTransition(previousState, &transition))
			{
				commandList->ResourceBarrier(1, &transition);
			}
		});

		textureManager->SetTextureState(texture, D3D12_RESOURCE_STATE_COMMON, commandList);

		commandList->Close();

		return textureManager->EndTextureWrite(texture);
	}

	void GenerateRenderSurfaces()
	{
		ID3D12Device5* device = DX12Rendering::Device::GetDevice();
		assert(device != nullptr);

		// Create RTV Heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
			rtvHeapDesc.NumDescriptors = static_cast<UINT>(ViewRenderTarget.size());
			rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
		}

		// Create DSV Heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = static_cast<UINT>(ViewDepthStencils.size());
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));
		}

		// Create all the surfaces
		m_surfaces.reserve(eRenderSurface::Count);

		{
			D3D12_CLEAR_VALUE clearValue = {};
			clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			clearValue.DepthStencil = { 1.0f, 0 };

			m_surfaces.emplace_back(L"DepthStencil", DXGI_FORMAT_D24_UNORM_S8_UINT, eRenderSurface::DepthStencil, RENDER_SURFACE_FLAG_NONE, clearValue);
		}

		{
			D3D12_CLEAR_VALUE clearValue = {}; // Set to unknown.

			m_surfaces.emplace_back(L"Diffuse", DXGI_FORMAT_R16G16B16A16_UNORM, eRenderSurface::Diffuse, RENDER_SURFACE_FLAG_ALLOW_UAV, clearValue);//Mark start here. We'll start seperating the diffuse and specular.
			m_surfaces.emplace_back(L"Specular", DXGI_FORMAT_R16G16B16A16_UNORM, eRenderSurface::Specular, RENDER_SURFACE_FLAG_ALLOW_UAV, clearValue);
			
			m_surfaces.emplace_back(L"Normal", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::Normal, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"FlatNormal", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::FlatNormal, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"FlatTangent", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::FlatTangent, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"Position", DXGI_FORMAT_R32G32B32A32_FLOAT, eRenderSurface::Position, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"Albedo", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::Albedo, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"SpecularColor", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::SpecularColor, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"Reflectivity", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::Reflectivity, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"MaterialProperties", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::MaterialProperties, RENDER_SURFACE_FLAG_NONE, clearValue);

			m_surfaces.emplace_back(L"RaytraceShadowMask", DXGI_FORMAT_R8G8B8A8_UINT, eRenderSurface::RaytraceShadowMask, RENDER_SURFACE_FLAG_ALLOW_UAV, clearValue);
			m_surfaces.emplace_back(L"Global Illumination", DXGI_FORMAT_R16G16B16A16_UNORM, eRenderSurface::GlobalIllumination, RENDER_SURFACE_FLAG_ALLOW_UAV, clearValue);

			m_surfaces.emplace_back(L"ReflectionVector", DXGI_FORMAT_R10G10B10A2_UNORM, eRenderSurface::ReflectionVector, RENDER_SURFACE_FLAG_NONE, clearValue);
			m_surfaces.emplace_back(L"Reflections", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::Reflections, RENDER_SURFACE_FLAG_NONE, clearValue);

			m_surfaces.emplace_back(L"RenderTarget1", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::RenderTarget1, RENDER_SURFACE_FLAG_SWAPCHAIN, clearValue);
			m_surfaces.emplace_back(L"RenderTarget2", DXGI_FORMAT_R8G8B8A8_UNORM, eRenderSurface::RenderTarget2, RENDER_SURFACE_FLAG_SWAPCHAIN, clearValue);
		}
	}

	void DestroySurfaces()
	{
		m_dsvHeap = nullptr;
		m_rtvHeap = nullptr;

		m_surfaces.clear();
	}

	RenderSurface* GetSurface(eRenderSurface surface)
	{
		assert(surface >= 0 && surface < eRenderSurface::Count);
		auto loc = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

		return &m_surfaces.at(surface);
	}
}