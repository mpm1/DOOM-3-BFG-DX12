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
			ID3D12Device5* GetDevice() { return m_device; }

			DeviceManager(IDXGIAdapter1* hardwareAdapter);
			~DeviceManager();

		private:
			ID3D12Device5* m_device;
		};


		DeviceManager* m_managerInstance = nullptr;

		void InitializeDevice(IDXGIFactory4* factory);
		void DestroyDevice();

		const DeviceManager* GetDeviceManager() { return m_managerInstance; }
		ID3D12Device5* GetDevice() { return GetDeviceManager() ? nullptr : m_managerInstance->GetDevice(); }
	}
}

#endif