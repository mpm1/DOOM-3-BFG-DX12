#ifndef __DX12_RENDERTARGET_H__
#define __DX12_RENDERTARGET_H__

#include "./dx12_resource.h"

namespace DX12Rendering {
	struct RenderTarget : public Resource
	{
		RenderTarget(const DXGI_FORMAT format, const LPCWSTR name);
		~RenderTarget();

		bool Resize(UINT width, UINT height);

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
	};

	struct DepthBuffer : public Resource
	{
		DepthBuffer(const LPCWSTR name);
		~DepthBuffer();
	};
}

#endif