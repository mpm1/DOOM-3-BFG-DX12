#ifndef __DX12_DEVICE_MANAGER_H__
#define __DX12_DEVICE_MANAGER_H__

#include "./dx12_global.h"
#include <functional>

namespace DX12Rendering
{
	namespace Device
	{
		typedef std::function<void(UINT, UINT, UINT, UINT, UINT, UINT)> DeviceResolutionFunc; //(UINT monitor, UINT subIndex, UINT width, UINT height, UINT refreshNumerator, UINT refreshDenominator)

		class DeviceManager
		{
		public:
			ID3D12Device5* GetDevice() { return m_device.Get(); }

			DeviceManager(IDXGIAdapter1* hardwareAdapter);
			~DeviceManager();

		private:
			ComPtr<ID3D12Device5> m_device;
		};

		void InitializeDevice(IDXGIFactory6* factory);
		void DestroyDevice();

		const DeviceManager* GetDeviceManager();
		ID3D12Device5* GetDevice();

		bool GetAllSupportedResolutions(const UINT monitor, Device::DeviceResolutionFunc resolutionCallback);

#ifdef DEBUG_GPU
		void __stdcall OnDeviceRemoved(PVOID context, BOOLEAN);
#endif
	}
}

#endif