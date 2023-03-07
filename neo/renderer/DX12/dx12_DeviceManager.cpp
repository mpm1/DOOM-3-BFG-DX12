#pragma hdrstop

#include "./dx12_DeviceManager.h"

#include <comdef.h>

namespace DX12Rendering
{
	using namespace Microsoft::WRL;

#ifdef DEBUG_GPU
	void __stdcall Device::OnDeviceRemoved(PVOID context, BOOLEAN) {
		ID3D12Device* removedDevice = (ID3D12Device*)context;
		HRESULT removedReason = removedDevice->GetDeviceRemovedReason();

		ComPtr<ID3D12DeviceRemovedExtendedData1> pDred;
		removedDevice->QueryInterface(IID_PPV_ARGS(&pDred)); //TODO: Validate result
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 DredAutoBreadcrumbsOutput;
		D3D12_DRED_PAGE_FAULT_OUTPUT1 DredPageFaultOutput;
		pDred->GetAutoBreadcrumbsOutput1(&DredAutoBreadcrumbsOutput); //TODO: Validate result
		pDred->GetPageFaultAllocationOutput1(&DredPageFaultOutput); //TODO: Validate result

		_com_error err(removedReason);
		DX12Rendering::FailMessage(err.ErrorMessage());
	}
#endif

	void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter) {
		*ppAdapter = nullptr;

		for (UINT adapterIndex = 0; ; ++adapterIndex) {
			IDXGIAdapter1* pAdapter = nullptr;
			if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &pAdapter)) {
				// No more adapters.
				break;
			}

			//TODO: Select the appropriate monitor.
			DXGI_ADAPTER_DESC1 desc;
			pAdapter->GetDesc1(&desc);

			// Check to see if the adapter supports Direct3D 12
			if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr))) {
				*ppAdapter = pAdapter;
				return;
			}

			pAdapter->Release();
		}
	}

	Device::DeviceManager* m_managerInstance = nullptr;

	void Device::InitializeDevice(IDXGIFactory4* factory)
	{
		if (m_managerInstance == nullptr)
		{
			// TODO: Try to enable a WARP adapter? I don't think we need to do this, since we expect DXR hardware.
			ComPtr<IDXGIAdapter1> hardwareAdapter;
			GetHardwareAdapter(factory, &hardwareAdapter);

			m_managerInstance = new DeviceManager(hardwareAdapter.Get());
		}
	}

	void Device::DestroyDevice()
	{
		if (m_managerInstance != nullptr)
		{
			delete m_managerInstance;
			m_managerInstance = nullptr;
		}
	}

	const Device::DeviceManager* Device::GetDeviceManager()
	{ 
		return m_managerInstance; 
	}

	ID3D12Device5* Device::GetDevice()
	{
		const Device::DeviceManager* manager = Device::GetDeviceManager();
		
		if (manager != nullptr)
		{
			return m_managerInstance->GetDevice();
		}
	}

	Device::DeviceManager::DeviceManager(IDXGIAdapter1* hardwareAdapter)
	{
		DX12Rendering::ThrowIfFailed(D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
	}

	Device::DeviceManager::~DeviceManager()
	{
		if (m_device != nullptr)
		{
			m_device = nullptr;
		}
	}
}