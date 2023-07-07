#ifndef __DX12_DEVICE_MANAGER_H__
#define __DX12_DEVICE_MANAGER_H__

#include "./dx12_global.h"

namespace DX12Rendering
{
	namespace Device
	{
		class DeviceManager
		{
		public:
			ID3D12Device5* GetDevice() { return m_device.Get(); }

			DeviceManager(IDXGIAdapter1* hardwareAdapter);
			~DeviceManager();

		private:
			ComPtr<ID3D12Device5> m_device;
		};

		void InitializeDevice(IDXGIFactory4* factory);
		void DestroyDevice();

		const DeviceManager* GetDeviceManager();
		ID3D12Device5* GetDevice();

#ifdef DEBUG_GPU
		void __stdcall OnDeviceRemoved(PVOID context, BOOLEAN);
#endif
	}
}

#endif