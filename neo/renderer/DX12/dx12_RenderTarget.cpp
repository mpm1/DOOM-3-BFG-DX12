#pragma hdrstop

#include "./dx12_RenderTarget.h"

namespace DX12Rendering
{
	RenderTarget::RenderTarget(const DXGI_FORMAT format, const LPCWSTR name) :
		Resource(name),
		m_width(0),
		m_height(0),
		m_lastTransitionState(D3D12_RESOURCE_STATE_STREAM_OUT),
		m_format(format)
	{
	}

	RenderTarget::~RenderTarget()
	{
	}

	bool RenderTarget::Resize(UINT width, UINT height)
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
		description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		description.Width = m_width;
		description.Height = m_height;
		description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		description.MipLevels = 1;
		description.SampleDesc.Count = 1;

		if (Allocate(description, D3D12_RESOURCE_STATE_COPY_SOURCE, kDefaultHeapProps) != nullptr)
		{
			resourceUpdated = true;
		}

		m_lastTransitionState = D3D12_RESOURCE_STATE_COPY_SOURCE;

		return resourceUpdated;
	}

	bool RenderTarget::TryTransition(const D3D12_RESOURCE_STATES toTransition, D3D12_RESOURCE_BARRIER* resourceBarrier)
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

	DepthBuffer::DepthBuffer(const LPCWSTR name) :
		Resource(name)
	{
	}
	DepthBuffer::~DepthBuffer()
	{
	}
}