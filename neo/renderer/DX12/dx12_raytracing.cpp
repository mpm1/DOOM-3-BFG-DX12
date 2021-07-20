#pragma hdrstop

#include "./dx12_raytracing.h"

DX12Raytracing::DX12Raytracing(ID3D12Device5* device)
	: m_device(device),
	isRaytracingSupported(CheckRaytracingSupport(device))
{
}

DX12Raytracing::~DX12Raytracing() 
{

}

bool DX12Raytracing::CheckRaytracingSupport(ID3D12Device5* device) {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};

	if(!DX12WarnIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))){
		return false;
	}

	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
		DX12WarnMessage("Raytracing Tier is not supported.");
		return false;
	}

	return true;
}