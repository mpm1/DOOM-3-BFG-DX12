#pragma hdrstop

#include "../../idlib/precompiled.h"

#include "../tr_local.h"

#include <comdef.h>

idCVar r_useRaytracedShadows("r_useRaytracedShadows", "1", CVAR_RENDERER | CVAR_BOOL, "use raytracing for shadows instead of stencil or baked shadows.");
//TODO: Implement the other raytraced effects.
idCVar r_useRaytracedReflections("r_useRaytracedReflections", "0", CVAR_RENDERER | CVAR_BOOL, "use raytracing for reflections instead of environment maps.");
idCVar r_useGlobalIllumination("r_useGlobalIllumination", "0", CVAR_RENDERER | CVAR_BOOL, "use raytraced global illumination for ambient lighting.");

DX12Renderer dxRenderer;
extern idCommon* common;

void __stdcall OnDeviceRemoved(PVOID context, BOOLEAN) {
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

DX12Renderer::DX12Renderer() :
	m_frameIndex(0),
	m_rtvDescriptorSize(0),
	m_width(2),
	m_height(2),
	m_fullScreen(0),
	m_rootSignature(nullptr),
	m_raytracing(nullptr)
{
}

DX12Renderer::~DX12Renderer() {
	OnDestroy();
}

void DX12Renderer::Init(HWND hWnd) {
	LoadPipeline(hWnd);
	LoadAssets();

	if (r_useRaytracedShadows.GetBool() || r_useRaytracedReflections.GetBool() || r_useGlobalIllumination.GetBool()) {
		m_raytracing = new DX12Rendering::Raytracing(m_device.Get(), m_width, m_height);
	}

	m_initialized = true;
}

void DX12Renderer::LoadPipeline(HWND hWnd) {
#if defined(_DEBUG)
#if !defined(USE_PIX)
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}

		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)))) {
			// Turn on auto-breadcrumbs and page fault reporting.
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}
#endif
#endif

	ComPtr<IDXGIFactory4> factory;
	DX12Rendering::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	// TODO: Try to enable a WARP adapter? I don't think we need to do this, since we expect DXR hardware.
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		DX12Rendering::ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
	}

	// Describe and create the command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	DX12Rendering::ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_directCommandQueue)));

	// Describe and create the copy command queue
	D3D12_COMMAND_QUEUE_DESC copyQueueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

	DX12Rendering::ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyCommandQueue)));

	// Describe and create the swap chain
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = DX12_FRAME_COUNT;
	swapChainDesc.BufferDesc.Width = m_width;
	swapChainDesc.BufferDesc.Height = m_height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO: Look into changing this for HDR
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE; //TODO: Change to option

	ComPtr<IDXGISwapChain> swapChain;
	DX12Rendering::ThrowIfFailed(factory->CreateSwapChain(m_directCommandQueue.Get(), &swapChainDesc, &swapChain));

	DX12Rendering::ThrowIfFailed(swapChain.As(&m_swapChain));

	// Remove ALT+ENTER functionality.
	DX12Rendering::ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (!CreateBackBuffer()) {
		common->FatalError("Could not initailze backbuffer.");
	}

	// Create Frame Resources
	ZeroMemory(&this->m_constantBuffer, sizeof(m_constantBuffer));

	// Create the command allocators
	for (int frame = 0; frame < DX12_FRAME_COUNT; ++frame)  {
		WCHAR nameDest[25];
		wsprintfW(nameDest, L"Main Command Allocator %d", frame);
		
		DX12Rendering::ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_directCommandAllocator[frame])));
		m_directCommandAllocator[frame]->SetName(nameDest);
	}

	DX12Rendering::ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator)));
	m_copyCommandAllocator->SetName(L"Main Command Allocator");
}

bool DX12Renderer::CreateBackBuffer() {
	// Describe and create a render target view (RTV) descriptor
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = DX12_FRAME_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)))) {
		return false;
	}

	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Describe and create a depth-stencil view (DSV) descriptor
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)))) {
		return false;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex) {
		// Create RTV for each frame
		if (FAILED(m_swapChain->GetBuffer(frameIndex, IID_PPV_ARGS(&m_renderTargets[frameIndex])))) {
			return false;
		}

		m_device->CreateRenderTargetView(m_renderTargets[frameIndex].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);

		WCHAR nameDest[16];
		wsprintfW(nameDest, L"Render Target %d", frameIndex);

		m_renderTargets[frameIndex]->SetName(static_cast<LPCWSTR>(nameDest));
	}

	// Create the DSV
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil = { 1.0f, 128 };

	if (FAILED(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&m_depthBuffer)
	))) {
		return false;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsv.Texture2D.MipSlice = 0;
	dsv.Flags = D3D12_DSV_FLAG_NONE;

	m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	return true;
}

void DX12Renderer::LoadPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC* psoDesc, ID3D12PipelineState** ppPipelineState) {
	assert(ppPipelineState != NULL);

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	psoDesc->pRootSignature = m_rootSignature->GetRootSignature();

	DX12Rendering::ThrowIfFailed(m_device->CreateGraphicsPipelineState(psoDesc, IID_PPV_ARGS(ppPipelineState)));	
}

void DX12Renderer::SetActivePipelineState(ID3D12PipelineState* pPipelineState) {
	if (pPipelineState != NULL  && pPipelineState != m_activePipelineState) {
		m_activePipelineState = pPipelineState;

		if (m_isDrawing) {
			m_commandList->SetPipelineState(pPipelineState);
		}
	}
}

void DX12Renderer::Uniform4f(UINT index, const float* uniform) {
	memcpy(&m_constantBuffer[index], uniform, sizeof(XMFLOAT4));
}

void DX12Renderer::LoadAssets() {
	// Create the synchronization objects
	{
		DX12Rendering::ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		DX12Rendering::ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence)));
		m_copyFenceValue = 1;

		// Create an event handle to use for the frame synchronization
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr) {
			DX12Rendering::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Attach event for device removal
#if defined(_DEBUG)
		m_removeDeviceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_removeDeviceEvent == nullptr) {
			DX12Rendering::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}
		m_fence->SetEventOnCompletion(UINT64_MAX, m_removeDeviceEvent); // This is done because all fence values are set the to  UINT64_MAX value when the device is removed.

		RegisterWaitForSingleObject(
			&m_deviceRemovedHandle,
			m_removeDeviceEvent,
			OnDeviceRemoved,
			m_device.Get(),
			INFINITE,
			0
		);
#endif

		// Wait for the command list to execute
		SignalNextFrame();
		WaitForPreviousFrame();
	}

	// Create Empty Root Signature
	m_rootSignature = new DX12RootSignature(m_device.Get(), sizeof(m_constantBuffer));

	// Create the Command Lists
	{
		DX12Rendering::ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_directCommandAllocator[m_frameIndex].Get(), NULL, IID_PPV_ARGS(&m_commandList)));
		DX12Rendering::ThrowIfFailed(m_commandList->Close());

		WCHAR nameDest[16];
		wsprintfW(nameDest, L"Command List %d", 0);

		m_commandList->SetName(static_cast<LPCWSTR>(nameDest));
	}

	// Create the Copy Command Lists
	{
		DX12Rendering::ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyCommandAllocator.Get(), NULL, IID_PPV_ARGS(&m_copyCommandList)));
		DX12Rendering::ThrowIfFailed(m_copyCommandList->Close());

		WCHAR nameDest[20];
		wsprintfW(nameDest, L"Copy Command List %d", 0);

		m_copyCommandList->SetName(static_cast<LPCWSTR>(nameDest));
	}

	{
		// Create the texture upload heap.
		//TODO: Find a better way and size for textures
		// For now we will assume that the max texture resolution is 1024x1024 32bit pixels
		const UINT bWidth = 1920;
		const UINT bHeight = 1080;
		const UINT bBytesPerRow = bWidth * 4;
		const UINT64 textureUploadBufferSize = (((bBytesPerRow + 255) & ~255) * (bHeight - 1)) + bBytesPerRow;

		DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_textureBufferUploadHeap)));

		m_textureBufferUploadHeap->SetName(L"Texture Buffer Upload Resource Heap");

		std::fill(m_activeTextures, m_activeTextures + TEXTURE_REGISTER_COUNT, static_cast<DX12TextureBuffer*>(nullptr));
	}
}

DX12VertexBuffer* DX12Renderer::AllocVertexBuffer(DX12VertexBuffer* buffer, UINT numBytes) {
	DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(numBytes),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer->vertexBuffer)
	));

	buffer->vertexBufferView.BufferLocation = buffer->vertexBuffer->GetGPUVirtualAddress();
	buffer->vertexBufferView.StrideInBytes = sizeof(idDrawVert); // TODO: Change to Doom vertex structure
	buffer->vertexBufferView.SizeInBytes = numBytes;

	return buffer;
}

void DX12Renderer::FreeVertexBuffer(DX12VertexBuffer* buffer) {
	buffer->vertexBuffer->Release();
}

DX12IndexBuffer* DX12Renderer::AllocIndexBuffer(DX12IndexBuffer* buffer, UINT numBytes) {
	DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(numBytes),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer->indexBuffer)
	));

	buffer->indexBufferView.BufferLocation = buffer->indexBuffer->GetGPUVirtualAddress();
	buffer->indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	buffer->indexBufferView.SizeInBytes = numBytes;

	return buffer;
}

void DX12Renderer::FreeIndexBuffer(DX12IndexBuffer* buffer) {
	buffer->indexBuffer->Release();
}

DX12JointBuffer* DX12Renderer::AllocJointBuffer(DX12JointBuffer* buffer, UINT numBytes) {
	// Create the buffer size.
	constexpr UINT resourceAlignment = (1024 * 64) - 1; // Resource must be a multible of 64KB
	constexpr UINT entrySize = ((sizeof(float) * 4 * 404) + 255) & ~255; // (Size of float4 * maxFloatAllowed) that's 256 byts aligned.
	const UINT heapSize = (numBytes + resourceAlignment) & ~resourceAlignment;

	// TODO: SET THIS FOR THE UPLOAD HEAP... THEN DO A SEPARATE JOINT HEAP JUST LIKE CBV
	DX12Rendering::ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(heapSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, // Currently not clear value needed
		IID_PPV_ARGS(&buffer->jointBuffer)
	));

	buffer->jointBuffer->SetName(L"Joint Upload Heap");
	buffer->entrySizeInBytes = entrySize;

	return buffer;
}

void DX12Renderer::FreeJointBuffer(DX12JointBuffer* buffer) {
	buffer->jointBuffer->Release();
}

void DX12Renderer::SetJointBuffer(DX12JointBuffer* buffer, UINT jointOffset, DX12Object* storedObject) {
	D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor = m_rootSignature->SetJointDescriptorTable(buffer, jointOffset, m_frameIndex, m_commandList.Get());

	if (storedObject != nullptr) {
		storedObject->jointView = descriptor;
		storedObject->includeJointView = true;
	}
}

void DX12Renderer::SignalNextFrame() {
	DX12Rendering::ThrowIfFailed(m_directCommandQueue->Signal(m_fence.Get(), m_fenceValue));
}

void DX12Renderer::WaitForPreviousFrame() {
	const UINT16 fence = m_fenceValue;

	// Wait for the previous frame to finish
	if (m_fence->GetCompletedValue() < fence) {
		DX12Rendering::ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void DX12Renderer::WaitForCopyToComplete() {
	const UINT64 fence = m_copyFenceValue;
	DX12Rendering::ThrowIfFailed(m_copyCommandQueue->Signal(m_copyFence.Get(), fence));
	++m_copyFenceValue;

	// Wait for the frame to finish
	if (m_copyFence->GetCompletedValue() < fence) {
		DX12Rendering::ThrowIfFailed(m_copyFence->SetEventOnCompletion(fence, m_copyFenceEvent));
		WaitForSingleObject(m_copyFenceEvent, INFINITE);
	}
}

void DX12Renderer::ResetCommandList(bool waitForBackBuffer) {
	if (!m_isDrawing) {
		return;
	}

	DX12Rendering::ThrowIfFailed(m_commandList->Reset(m_directCommandAllocator[m_frameIndex].Get(), m_activePipelineState));
	m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());

	if (waitForBackBuffer) {
		// Indicate that we will be rendering to the back buffer
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	}

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	m_commandList->OMSetStencilRef(m_stencilRef);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Setup the initial heap location
	ID3D12DescriptorHeap* descriptorHeaps[1] = { 
		m_rootSignature->GetCBVHeap(m_frameIndex),
	};	
	m_commandList->SetDescriptorHeaps(1, descriptorHeaps);
}

void DX12Renderer::ExecuteCommandList() {
	if (!m_isDrawing) {
		return;
	}

	//TODO: Implement version for multiple command lists
	DX12Rendering::WarnIfFailed(m_commandList->Close());

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_directCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void DX12Renderer::BeginDraw() {
	if (m_isDrawing || !m_initialized) {
		return;
	}

	WaitForPreviousFrame();
	++m_fenceValue;

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_rootSignature->BeginFrame(m_frameIndex);
	
	m_objects.empty();
	m_objectIndex = 0;
	m_isDrawing = true;
	DX12Rendering::ThrowIfFailed(m_directCommandAllocator[m_frameIndex]->Reset()); //TODO: Change to warning

	ResetCommandList(true);
}

void DX12Renderer::EndDraw() {
	if (!m_isDrawing) {
		return;
	}

	// present the backbuffer
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	m_isDrawing = false;
	
	//common->Printf("%d heap objects registered.\n", m_cbvHeapIndex);
}

UINT DX12Renderer::StartSurfaceSettings() {
	assert(m_isDrawing);
	++m_objectIndex;

	if (m_objectIndex >= MAX_OBJECT_COUNT) {
		m_objectIndex = 0;
	}

	return m_objectIndex;
}

bool DX12Renderer::EndSurfaceSettings(DX12Object* storedObject) {
	// TODO: Define separate CBV for location data and Textures
	// TODO: add a check if we need to update tehCBV and Texture data.

	assert(m_isDrawing);

	if (!DX12_ActivatePipelineState()) {
		// We cant draw the object, so return.
		return false;
	}
	
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvView = m_rootSignature->SetCBVDescriptorTable(sizeof(m_constantBuffer), m_constantBuffer, m_objectIndex, m_frameIndex, m_commandList.Get());

	// Copy the Textures
	DX12TextureBuffer* currentTexture;
	UINT index;
	for (index = 0; index < TEXTURE_REGISTER_COUNT && (currentTexture = m_activeTextures[index]) != nullptr; ++index) {
		SetTexturePixelShaderState(currentTexture);
		m_rootSignature->SetTextureRegisterIndex(index, currentTexture, m_frameIndex, m_commandList.Get());
	}

	if (storedObject != nullptr) {
		storedObject->pipelineState = m_activePipelineState;
		storedObject->cbvView = cbvView;
		
		// TODO: right now we're just setting the depth objects. We need to update this for all stages.
		dxRenderer.AddStageToObject(storedObject, DEPTH_STAGE, index, m_activeTextures);
	}

	return true;
}

bool DX12Renderer::IsScissorWindowValid() {
	return m_scissorRect.right > m_scissorRect.left && m_scissorRect.bottom > m_scissorRect.top;
}

void DX12Renderer::PresentBackbuffer() {
	// Present the frame
	DX12Rendering::ThrowIfFailed(m_swapChain->Present(1, 0));
	
	SignalNextFrame();
	WaitForPreviousFrame();
}

void DX12Renderer::DrawModel(DX12VertexBuffer* vertexBuffer, UINT vertexOffset, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount) {
	if (!IsScissorWindowValid()) {
		return;
	}

	D3D12_VERTEX_BUFFER_VIEW vertecies = vertexBuffer->vertexBufferView;

	D3D12_INDEX_BUFFER_VIEW indecies = indexBuffer->indexBufferView;

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &vertecies);
	m_commandList->IASetIndexBuffer(&indecies);

	// Draw the model
	m_commandList->DrawIndexedInstanced(indexCount, 1, indexOffset, vertexOffset, 0); // TODO: Multiply by 16 for index?
}

void DX12Renderer::Clear(bool color, bool depth, bool stencil, byte stencilValue, float* colorRGBA) {
	if (!m_isDrawing) {
		return;
	}

	uint8 clearFlags = 0;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	

	if (color) {
		m_commandList->ClearRenderTargetView(rtvHandle, colorRGBA, 0, nullptr);
	}

	if (depth) {
		clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
	}

	if (stencil) {
		// TODO: Implement stencil first.
		clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
	}

	if (clearFlags > 0) {
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
		m_commandList->ClearDepthStencilView(dsvHandle, static_cast<D3D12_CLEAR_FLAGS>(clearFlags), 1.0f, stencilValue, 0, nullptr);
	}
}

void DX12Renderer::OnDestroy() {
	WaitForPreviousFrame();

	CloseHandle(m_fenceEvent);

	if (m_raytracing != nullptr) {
		delete m_raytracing;
		m_raytracing = nullptr;
	}

	if (m_rootSignature != nullptr) {
		delete m_rootSignature;
		m_rootSignature = nullptr;
	}

	m_initialized = false;
}

bool DX12Renderer::SetScreenParams(UINT width, UINT height, int fullscreen)
{
	/*if (m_width == width && m_height == height && m_fullScreen == fullscreen) {
		return true;
	}*/
	// TODO: Resize buffers as needed.

	m_width = width;
	m_height = height;
	m_fullScreen = fullscreen;
	m_aspectRatio = static_cast<FLOAT>(width) / static_cast<FLOAT>(height);

	// TODO: Find the correct window.
	//m_swapChain->SetFullscreenState(true, NULL);

	// TODO: HANDLE THIS WHILE DRAWING.
	if (m_device && m_swapChain && m_directCommandAllocator[m_frameIndex]) {
		if (!m_isDrawing) {
			WaitForPreviousFrame();
			++m_fenceValue;

			if (FAILED(m_directCommandAllocator[m_frameIndex]->Reset())) {
				common->Warning("DX12Renderer::SetScreenParams: Error resetting command allocator.");
				return false;
			}

			if (FAILED(m_commandList->Reset(m_directCommandAllocator[m_frameIndex].Get(), nullptr))) {
				common->Warning("DX12Renderer::SetScreenParams: Error resetting command list.");
				return false;
			}
		}

		for (int frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex) {
			m_renderTargets[frameIndex].Reset();
		}

		if (FAILED(m_swapChain->ResizeBuffers(DX12_FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, NULL))) {
			return false;
		}

		m_frameIndex = 0;
		if (!CreateBackBuffer()) {
			return false;
		}

		m_raytracing->Resize(m_width, m_height);

		UpdateViewport(0.0f, 0.0f, width, height);
		UpdateScissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));

		if (!m_isDrawing) {
			if (FAILED(m_commandList->Close())) {
				return false;
			}

			ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
			m_directCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
		}

		SignalNextFrame();
	}
	else {
		UpdateViewport(0.0f, 0.0f, width, height);
		UpdateScissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));
	}

	return true;
}

void DX12Renderer::UpdateViewport(const FLOAT topLeftX, const FLOAT topLeftY, const FLOAT width, const FLOAT height, const FLOAT minDepth, const FLOAT maxDepth) {
	m_viewport.TopLeftX = topLeftX;
	m_viewport.TopLeftY = topLeftY;
	m_viewport.Width = width;
	m_viewport.Height = height;
	m_viewport.MinDepth = minDepth;
	m_viewport.MaxDepth = maxDepth;
}

void DX12Renderer::UpdateScissorRect(const LONG x, const LONG y, const LONG w, const LONG h) {
	m_scissorRect.left = x;
	m_scissorRect.right = x + w;

	// Note: x and y are the lower left corner  of the scissor window. We need to calculate the y location to work properly with DirectX. 
	m_scissorRect.bottom = m_viewport.Height - y;
	m_scissorRect.top = m_scissorRect.bottom - h;

	if (m_isDrawing) {
		m_commandList->RSSetScissorRects(1, &m_scissorRect);
	}
}

void DX12Renderer::UpdateStencilRef(UINT ref) {
	if (m_stencilRef != ref) {
		m_stencilRef = ref;

		if (m_isDrawing) {
			m_commandList->OMSetStencilRef(ref);
		}
	}
}

void DX12Renderer::ReadPixels(int x, int y, int width, int height, UINT readBuffer, byte* buffer) {
	// TODO: Implement
	common->Warning("Read Pixels not yet implemented.");
}

// Texture functions
void DX12Renderer::SetActiveTextureRegister(UINT8 index) {
	if (index < 5) {
		m_activeTextureRegister = index;
	}
}

DX12TextureBuffer* DX12Renderer::AllocTextureBuffer(DX12TextureBuffer* buffer, D3D12_RESOURCE_DESC* textureDesc, const idStr* name) {
	// Create the buffer object.
	if (!DX12Rendering::WarnIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer->textureBuffer)))) {
		return nullptr;
	}

	// Add a name to the property.
	wchar_t wname[256];
	wsprintfW(wname, L"Texture: %hs", name->c_str());
	buffer->textureBuffer->SetName(wname);

	// Create the Shader Resource View
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = textureDesc->Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = textureDesc->MipLevels;

	buffer->textureView = srvDesc;
	buffer->usageState = D3D12_RESOURCE_STATE_COPY_DEST;
	buffer->name = name;

	return buffer;
}

void DX12Renderer::FreeTextureBuffer(DX12TextureBuffer* buffer) {
	if (buffer != nullptr) {
		WaitForCopyToComplete();
		WaitForPreviousFrame();
		delete(buffer);
	}
}

void DX12Renderer::SetTextureContent(DX12TextureBuffer* buffer, const UINT mipLevel, const UINT bytesPerRow, const size_t imageSize, const void* image) {
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = image;
	textureData.RowPitch = bytesPerRow;
	textureData.SlicePitch = imageSize;

	int intermediateOffset = 0;
	if (mipLevel > 0) {
		UINT mipCheck = mipLevel;
		size_t lastSize = imageSize << 2;

		for (; mipCheck > 0; --mipCheck) {
			intermediateOffset += ((lastSize + 511) & ~511); // 512 byte align.
			lastSize = lastSize << 2;
		}
	}

	SetTextureCopyState(buffer, mipLevel);
	UpdateSubresources(m_copyCommandList.Get(), buffer->textureBuffer.Get(), m_textureBufferUploadHeap.Get(), intermediateOffset, mipLevel, 1, &textureData);
}

void DX12Renderer::SetTexture(DX12TextureBuffer* buffer) {
	m_activeTextures[m_activeTextureRegister] = buffer;
}

bool DX12Renderer::SetTextureCopyState(DX12TextureBuffer* buffer, const UINT mipLevel) {
	return SetTextureState(buffer, D3D12_RESOURCE_STATE_COPY_DEST, m_copyCommandList.Get(), mipLevel);
}

bool DX12Renderer::SetTexturePixelShaderState(DX12TextureBuffer* buffer, const UINT mipLevel) {
	return SetTextureState(buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, m_commandList.Get());
}

bool DX12Renderer::SetTextureState(DX12TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, ID3D12GraphicsCommandList* commandList, const UINT mipLevel) {
	if (buffer == nullptr) {
		return false;
	}

	if (buffer->usageState == usageState) {
		return false;
	}
	
	// TODO: Check for valid state transitions.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(buffer->textureBuffer.Get(), buffer->usageState, usageState, mipLevel));
	buffer->usageState = usageState;

	return true; // STate has changed.
}

void DX12Renderer::StartTextureWrite(DX12TextureBuffer* buffer) {
	WaitForCopyToComplete();

	if (FAILED(m_copyCommandAllocator->Reset())) {
		common->Warning("Could not reset the copy command allocator.");
		return;
	}

	if (FAILED(m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr))) {
		common->Warning("Could not reset the copy command list.");
		return;
	}
}

void DX12Renderer::EndTextureWrite(DX12TextureBuffer* buffer) {
	if (FAILED(m_copyCommandList->Close())) {
		common->Warning("Could not close copy command list.");
	}

	// Execute the command list
	ID3D12CommandList* ppCommandLists[] = { m_copyCommandList.Get() };
	m_copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	WaitForCopyToComplete();
}

DX12Object* DX12Renderer::AddToObjectList(const dxObjectIndex_t& key, DX12VertexBuffer* vertexBuffer, UINT vertexOffset, DX12IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount) {
	DX12Object object = {};
	object.index = key;
	object.pipelineState = nullptr;
	object.includeJointView = false;

	object.vertexBuffer = vertexBuffer;
	object.vertexOffset = vertexOffset;

	object.indexBuffer = indexBuffer;
	object.indexOffset = indexOffset;
	object.indexCount = indexCount;

	object.stages.clear();

	m_objects[key] = object;
	return &m_objects[key];
}

DX12Stage* DX12Renderer::AddStageToObject(DX12Object* storedObject, eStageType stageType, UINT textureCount, DX12TextureBuffer** textures) {
	storedObject->stages.push_back(DX12Stage());

	DX12Stage* stage = &storedObject->stages.back();
	stage->type = stageType;
	stage->textureCount = textureCount;
	std::copy(textures, textures + textureCount, stage->textures);

	return stage;
}

void DX12Renderer::BeginBottomLevelRayTracingSetup()
{
	assert(IsRaytracingEnabled(), "DX12Renderer::BeginBottomLevelRayTracingSetup - Raytracing is not enabled");

	m_raytracing->ResetFrame();
	// For now we will clear the object list and reset the BLAS.
	// TODO: Modify this to just update, and have a separate clear function.
}

void DX12Renderer::EndBottomLevelRayTracingSetup(std::function<void(const dxObjectIndex_t&, const DX12Object&)> onGenerateBLAS = nullptr)
{
	assert(IsRaytracingEnabled(), "DX12Renderer::EndBottomLevelRayTracingSetup - Raytracing is not enabled");

	for (auto& objectPair : m_objects)
	{
		m_raytracing->GenerateBottomLevelAS(&objectPair.second, false);

		if (onGenerateBLAS != nullptr)
		{
			onGenerateBLAS(objectPair.first, objectPair.second);
		}
	}

	m_raytracing->ExecuteCommandList();
	m_raytracing->ResetCommandList();
}

void DX12Renderer::BeginTopLevelRayTracingSetup() {
	assert(IsRaytracingEnabled(), "DX12Renderer::BeginTopLevelRayTracingSetup - Raytracing is not enabled");

	m_raytracing->StartAccelerationStructure(r_useRaytracedShadows.GetBool(), r_useRaytracedReflections.GetBool(), r_useGlobalIllumination.GetBool());
}

void DX12Renderer::EndTopLevelRayTracingSetup(const std::vector<dxObjectIndex_t>& objectIds) {
	assert(IsRaytracingEnabled(), "DX12Renderer::EndTopLevelRayTracingSetup - Raytracing is not enabled");
	
	for (const dxObjectIndex_t& index : objectIds) {
		DX12Object object = m_objects[index];
		m_raytracing->AddObjectToAllTopLevelAS(&object, false);
	}

	m_raytracing->EndAccelerationStructure();
}

void DX12Renderer::GenerateRaytracedStencilShadows()
{
	const UINT32 stencilIndex = 11; // TODO: Calculate this earlier
	m_raytracing->CastShadowRays(m_commandList.Get(), m_viewport, m_scissorRect, m_depthBuffer.Get(), 11);
}