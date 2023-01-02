#pragma hdrstop

#include "../../idlib/precompiled.h"

#include "../tr_local.h"

#include <comdef.h>
#include <type_traits>

idCVar r_useRayTraycing("r_useRayTraycing", "0", CVAR_RENDERER | CVAR_BOOL, "use the raytracing system for scene generation.");

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

template<typename T>
const dxHandle_t DX12Renderer::GetHandle(const T* entityHandle)
{
	std::static_assert(false, "Invalid type used for entity handle");
}

template<>
const dxHandle_t DX12Renderer::GetHandle<qhandle_t>(const qhandle_t* entityHandle)
{
	return static_cast<dxHandle_t>(*entityHandle);
}

template<>
const dxHandle_t DX12Renderer::GetHandle<idRenderEntityLocal>(const idRenderEntityLocal* entity)
{
	return static_cast<dxHandle_t>(entity->index);
}

template<>
const dxHandle_t DX12Renderer::GetHandle<viewLight_t>(const viewLight_t* vLight)
{
	return static_cast<dxHandle_t>(vLight->lightDef->index);
}

void DX12Renderer::Init(HWND hWnd) {
	LoadPipeline(hWnd);
	LoadAssets();

#ifdef DEBUG_IMGUI
	InitializeImGui(hWnd);
#endif

	if (r_useRayTraycing.GetBool()) {
		m_raytracing = new DX12Rendering::Raytracing(m_device.Get(), m_width, m_height);
	}

	m_initialized = true;
}

void DX12Renderer::LoadPipeline(HWND hWnd) {
#if defined(DEBUG_GPU)
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

	ComPtr<IDXGIFactory4> factory;
	DX12Rendering::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	// TODO: Try to enable a WARP adapter? I don't think we need to do this, since we expect DXR hardware.
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		DX12Rendering::ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
	}

	DX12Rendering::Commands::InitializeCommandLists(m_device.Get());

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

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
	DX12Rendering::ThrowIfFailed(factory->CreateSwapChain(commandList->GetCommandQueue(), &swapChainDesc, &swapChain));

	DX12Rendering::ThrowIfFailed(swapChain.As(&m_swapChain));

	// Remove ALT+ENTER functionality.
	DX12Rendering::ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (!CreateBackBuffer()) {
		common->FatalError("Could not initailze backbuffer.");
	}

	// Create Frame Resources
	ZeroMemory(&this->m_constantBuffer, sizeof(m_constantBuffer));
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
			DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT)->CommandSetPipelineState(pPipelineState);
		}
	}
}

void DX12Renderer::Uniform4f(UINT index, const float* uniform) {
	memcpy(&m_constantBuffer[index], uniform, sizeof(XMFLOAT4));
}

void DX12Renderer::LoadAssets() {
	// Create the synchronization objects
	{
		m_frameFence.Allocate(m_device.Get());

		// Attach event for device removal
#ifdef DEBUG_GPU
		m_removeDeviceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_removeDeviceEvent == nullptr) {
			DX12Rendering::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}
		m_frameFence.SetCompletionEvent(UINT64_MAX, m_removeDeviceEvent); // This is done because all fence values are set the to  UINT64_MAX value when the device is removed.

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

void DX12Renderer::SetJointBuffer(DX12JointBuffer* buffer, UINT jointOffset) {
	D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor = m_rootSignature->SetJointDescriptorTable(buffer, jointOffset, m_frameIndex, DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT));
}

void DX12Renderer::SignalNextFrame() {
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	m_frameFence.Signal(m_device.Get(), commandList->GetCommandQueue());
}

void DX12Renderer::WaitForPreviousFrame() {
	m_frameFence.Wait();
}

void DX12Renderer::WaitForCopyToComplete() {
	m_copyFence.Wait();
}

void DX12Renderer::SetCommandListDefaults(const bool resetPipelineState) {
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
	{
		auto rootSignature = m_rootSignature->GetRootSignature();
		if (rootSignature != nullptr)
		{
			commandList->SetGraphicsRootSignature(rootSignature);
		}

		if (m_activePipelineState != nullptr) {
			commandList->SetPipelineState(m_activePipelineState);
		}

		commandList->RSSetViewports(1, &m_viewport);
		commandList->RSSetScissorRects(1, &m_scissorRect);

		commandList->OMSetStencilRef(m_stencilRef);

		const CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
		const CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		// Setup the initial heap location
		ID3D12DescriptorHeap* descriptorHeaps[1] = {
			m_rootSignature->GetCBVHeap(m_frameIndex),
		};
		commandList->SetDescriptorHeaps(1, descriptorHeaps);
	});
}

void DX12Renderer::CycleDirectCommandList() {
	if (!m_isDrawing) {
		return;
	}

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	commandList->Cycle();

	SetCommandListDefaults(false);
}

void DX12Renderer::BeginDraw() {
	if (m_isDrawing || !m_initialized) {
		return;
	}

	//DX12Rendering::CaptureEventStart(m_commandList.Get(), "Begin Frame");

	WaitForPreviousFrame();

#ifdef _DEBUG
	DebugClearLights();
#endif

	DX12Rendering::Commands::CommandListsBeginFrame();

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_rootSignature->BeginFrame(m_frameIndex);
	
	m_objectIndex = 0;
	m_isDrawing = true;

	if (IsRaytracingEnabled())
	{
		m_raytracing->BeginFrame();
	}

	// Reset Allocators.
	if (m_copyFence.IsFenceCompleted()) {
		//if (FAILED(m_copyCommandAllocator->Reset())) {
		//	common->Warning("Could not reset the copy command allocator.");
		//	//return;
		//}
	}

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	commandList->CommandResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	SetCommandListDefaults(false);
}

void DX12Renderer::EndDraw() {
	if (!m_isDrawing) {
		return;
	}

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

#ifdef DEBUG_IMGUI
	DX12Rendering::ImGui_StartFrame();
	ImGuiDebugWindows();
	DX12Rendering::ImGui_EndFrame(m_imguiSrvDescHeap.Get());
#endif

	commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
	{
		// present the backbuffer
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	});

	DX12Rendering::Commands::CommandListsEndFrame();

	m_isDrawing = false;
	
	DX12Rendering::IncrementFrameIndex();
	//common->Printf("%d heap objects registered.\n", m_cbvHeapIndex);

	//DX12Rendering::CaptureEventEnd(m_commandList.Get());
}

UINT DX12Renderer::StartSurfaceSettings() {
	assert(m_isDrawing);
	++m_objectIndex;

	if (m_objectIndex >= MAX_OBJECT_COUNT) {
		m_objectIndex = 0;
	}

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	commandList->BeginCommandChunk();

	return m_objectIndex;
}

bool DX12Renderer::EndSurfaceSettings() {
	// TODO: Define separate CBV for location data and Textures
	// TODO: add a check if we need to update tehCBV and Texture data.

	assert(m_isDrawing);

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	if (!DX12_ActivatePipelineState()) {
		// We cant draw the object, so return.
		commandList->EndCommandChunk();
		return false;
	}
	
	const D3D12_CONSTANT_BUFFER_VIEW_DESC cbvView = m_rootSignature->SetCBVDescriptorTable(sizeof(m_constantBuffer), m_constantBuffer, m_objectIndex, m_frameIndex, commandList);

	// Copy the Textures
	DX12TextureBuffer* currentTexture;
	UINT index;
	for (index = 0; index < TEXTURE_REGISTER_COUNT && (currentTexture = m_activeTextures[index]) != nullptr; ++index) {
		SetTexturePixelShaderState(currentTexture);
		m_rootSignature->SetTextureRegisterIndex(index, currentTexture, m_frameIndex, commandList);
	}

	commandList->EndCommandChunk();

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

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	commandList->AddCommand([&indecies, &vertecies, &indexCount, &indexOffset, &vertexOffset](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
	{
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vertecies);
		commandList->IASetIndexBuffer(&indecies);

		// Draw the model
		commandList->DrawIndexedInstanced(indexCount, 1, indexOffset, vertexOffset, 0); // TODO: Multiply by 16 for index?
	});
}

void DX12Renderer::Clear(const bool color, const bool depth, const bool stencil, byte stencilValue, const float colorRGBA[4]) {
	if (!m_isDrawing) {
		return;
	}

	uint8 clearFlags = 0;
	if (depth) {
		clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
	}

	if (stencil) {
		// TODO: Implement stencil first.
		clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
	}

	if (color || clearFlags > 0)
	{
		auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

		commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			if (color) {
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
				commandList->ClearRenderTargetView(rtvHandle, colorRGBA, 0, nullptr);
			}

			if (clearFlags > 0) {
				CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
				commandList->ClearDepthStencilView(dsvHandle, static_cast<D3D12_CLEAR_FLAGS>(clearFlags), 1.0f, stencilValue, 0, nullptr);
			}
		});
	}
}

void DX12Renderer::OnDestroy() {
	WaitForPreviousFrame();

#ifdef DEBUG_IMGUI
	ReleaseImGui();
#endif

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
	if (m_device && m_swapChain) {
		if (m_isDrawing) {
			WaitForPreviousFrame();

			DX12Rendering::Commands::CommandListsEndFrame();
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

		if (m_raytracing != nullptr) {
			m_raytracing->Resize(m_width, m_height);
		}

		UpdateViewport(0.0f, 0.0f, width, height);
		UpdateScissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));

		if (!m_isDrawing) {
			auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

			commandList->Execute();
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
	m_scissorRect.right = m_scissorRect.left + w;

	// Note: x and y are the lower left corner  of the scissor window. We need to calculate the y location to work properly with DirectX. 
	m_scissorRect.top = y;
	m_scissorRect.bottom = m_scissorRect.top + h;
	//m_viewport.Height;

	if (m_isDrawing) {
		auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
		commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			commandList->RSSetScissorRects(1, &m_scissorRect);
		});
	}
}

void DX12Renderer::UpdateStencilRef(UINT ref) {
	if (m_stencilRef != ref) {
		m_stencilRef = ref;

		if (m_isDrawing) {
			auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
			commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
			{
				commandList->OMSetStencilRef(ref);
			});
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
		WaitForPreviousFrame();
		WaitForCopyToComplete();
		
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

	if (buffer != nullptr)
	{
		auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);

		copyCommands->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
		{
			SetTextureCopyState(buffer, mipLevel);
			UpdateSubresources(commandList, buffer->textureBuffer.Get(), m_textureBufferUploadHeap.Get(), intermediateOffset, mipLevel, 1, &textureData);
		});
	}
	else
	{
		// TODO: Fix on null buffer.
	}
}

void DX12Renderer::SetTexture(DX12TextureBuffer* buffer) {
	m_activeTextures[m_activeTextureRegister] = buffer;
}

bool DX12Renderer::SetTextureCopyState(DX12TextureBuffer* buffer, const UINT mipLevel) {
	auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);
	return SetTextureState(buffer, D3D12_RESOURCE_STATE_COPY_DEST, copyCommands, mipLevel);
}

bool DX12Renderer::SetTexturePixelShaderState(DX12TextureBuffer* buffer, const UINT mipLevel) {
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	return SetTextureState(buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, commandList);
}

bool DX12Renderer::SetTextureState(DX12TextureBuffer* buffer, const D3D12_RESOURCE_STATES usageState, DX12Rendering::Commands::CommandList* commandList, const UINT mipLevel) {
	if (buffer == nullptr) {
		return false;
	}

	if (buffer->usageState == usageState) {
		return false;
	}
	
	// TODO: Check for valid state transitions.
	commandList->CommandResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(buffer->textureBuffer.Get(), buffer->usageState, usageState, mipLevel));
	buffer->usageState = usageState;

	return true; // State has changed.
}

void DX12Renderer::StartTextureWrite(DX12TextureBuffer* buffer) {
	WaitForCopyToComplete();
	auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);

	copyCommands->Reset();
}

void DX12Renderer::EndTextureWrite(DX12TextureBuffer* buffer) {
	auto copyCommands = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COPY);
	copyCommands->SignalFence(m_copyFence);
	copyCommands->Cycle();
}

void DX12Renderer::DXR_ResetAccelerationStructure()
{
	
	if (!IsRaytracingEnabled()) {
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);
	
	m_raytracing->ResetGeneralTLAS();

	m_raytracing->GetBLAS()->Reset();

	// TODO: what else do we need to reset.
	//m_raytracing->ResetFrame();
}

void DX12Renderer::DXR_UpdateModelInBLAS(const qhandle_t modelHandle, const idRenderModel* model)
{
	if (!IsRaytracingEnabled() || model == nullptr) {
		return;
	}

	// TODO: Add support for joints.
	if (model->GetJoints() != NULL) {
		return;
	}

	// For now we are only supporting objects that can receive shadows.
	if (!model->ModelHasShadowCastingSurfaces())
	{
		return;
	}

	if (model->NumSurfaces() <= 0) {
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	const dxHandle_t index = GetHandle(&modelHandle);
	DX12Rendering::DX12AccelerationObject* result = m_raytracing->GetBLAS()->GetAccelerationObject(index);

	DX12VertexBuffer* vertexBuffer = nullptr;
	idIndexBuffer* indexBuffer = nullptr;

	const modelSurface_t& surf = *model->Surface(0);
	const vertCacheHandle_t& vbHandle = surf.geometry->ambientCache;
	const vertCacheHandle_t& ibHandle = surf.geometry->indexCache;

	int vertOffsetBytes;
	int indexOffset;

	// Get vertex buffer
	if (vertexCache.CacheIsStatic(vbHandle))
	{
		vertexBuffer = reinterpret_cast<DX12VertexBuffer*>(vertexCache.staticData.vertexBuffer.GetAPIObject());
		vertOffsetBytes = static_cast<int>(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

		indexBuffer = &vertexCache.staticData.indexBuffer;
		indexOffset = static_cast<int>(ibHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
	}
	else
	{
		return;
		/*if (!model->IsLoaded())
		{
			re->hModel->LoadModel();
		}*/

		/*std::vector<triIndex_t> indecies;
		std::vector<triIndex_t>::iterator iIterator = indecies.begin();
		std::vector<idDrawVert> vertecies;

		for (int sIndex = 0; sIndex < model->NumSurfaces(); ++sIndex)
		{
			// TODO: make separate for each material.
			const modelSurface_t* surf = re->hModel->Surface(sIndex);

			// Add all indecies
			iIterator = indecies.insert(iIterator, surf->geometry->indexes, surf->geometry->indexes + surf->geometry->numIndexes);

			// Add all vertecies
			vertecies.reserve(vertecies.size() + surf->geometry->numVerts);
			for (int vIndex = 0; vIndex < surf->geometry->numVerts; ++vIndex)
			{
				idDrawVert* vert = &surf->geometry->verts[vIndex];
				vertecies.push_back(*vert);
			}
		}

		m_raytracing->AddOrUpdateVertecies(m_device.Get(), modelHandle, reinterpret_cast<byte*>(&vertecies[0]), vertecies.size() * sizeof(idDrawVert));
		return;*/
	};

	if (result != nullptr)
	{
		//TODO: update
	}
	else
	{
		m_raytracing->GetBLAS()->AddAccelerationObject(
			index,
			vertexBuffer,
			vertOffsetBytes,
			surf.geometry->numVerts,
			reinterpret_cast<DX12IndexBuffer*>(indexBuffer->GetAPIObject()),
			indexOffset >> 1, // TODO: Figure out why we need to divide by 2. Is it because we are going from an int to a short?
			r_singleTriangle.GetBool() ? 3 : surf.geometry->numIndexes
		);
	}
}

void DX12Renderer::DXR_UpdateBLAS()
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	m_raytracing->GetBLAS()->Generate(m_device.Get(), m_raytracing->scratchBuffer.Get(), DEFAULT_SCRATCH_SIZE, L"Main Raytracing BLAS");
	
	//TODO: Move this to a more generalized execution tract.
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::COMPUTE);
	commandList->Execute();
	commandList->Reset();

	m_raytracing->CleanUpAccelerationStructure();
}

template<typename K>
DX12Rendering::TopLevelAccelerationStructure* DX12Renderer::DXR_GetOrGenerateAccelerationStructure(const K& keyHandle)
{
	std::static_assert(false, "Invalid key  handle for TLAS");
	return nullptr;
}

template<>
DX12Rendering::TopLevelAccelerationStructure* DX12Renderer::DXR_GetOrGenerateAccelerationStructure(const std::nullptr_t& nullKey)
{
	return m_raytracing->GetGeneralTLAS();
}

template<>
DX12Rendering::TopLevelAccelerationStructure* DX12Renderer::DXR_GetOrGenerateAccelerationStructure(const viewLight_t& keyHandle)
{
	idassert(false);// Light based TLAS no longer supported.
	return nullptr;

	/*const dxHandle_t handle = GetHandle(keyHandle);
	DX12Rendering::TopLevelAccelerationStructure* tlas = m_raytracing->GetGeneralTLAS(handle);

	if (tlas == nullptr)
	{
		tlas = m_raytracing->EmplaceShadowTLAS(handle);
	}

	return tlas;*/
}

template<typename K>
void DX12Renderer::DXR_AddEntityAccelerationStructure(const K& keyHandle, const dxHandle_t entityHandle, const float modelMatrix[16])
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	if (m_raytracing->m_blas.GetAccelerationObject(entityHandle) == nullptr) {
		common->DWarning("DX12Renderer::AddEntityAccelerationStructure: Invalid entity key %d", entityHandle);
		return;
	}

	DX12Rendering::TopLevelAccelerationStructure* tlas = DXR_GetOrGenerateAccelerationStructure(keyHandle);

	tlas->AddInstance(entityHandle, modelMatrix);
}

template<>
void DX12Renderer::DXR_AddEntityAccelerationStructure(const std::nullptr_t& keyHandle, const dxHandle_t entityHandle, const float modelMatrix[16])
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	if (m_raytracing->GetBLAS()->GetAccelerationObject(entityHandle) == nullptr) {
		common->DWarning("DX12Renderer::AddEntityAccelerationStructure: Invalid entity key %d", entityHandle);
		return;
	}

	DX12Rendering::TopLevelAccelerationStructure* tlas = DXR_GetOrGenerateAccelerationStructure(keyHandle);

	tlas->AddInstance(entityHandle, modelMatrix);
}

template<typename K>
void DX12Renderer::DXR_UpdateAccelerationStructure(const K& keyHandle)
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	//DX12Rendering::WriteLock raytraceLock(m_raytracingLock);
	DX12Rendering::TopLevelAccelerationStructure* tlas = DXR_GetOrGenerateAccelerationStructure(keyHandle);

	// Generate the TLAS resource
	m_raytracing->GenerateTLAS(tlas);
}

template<>
void DX12Renderer::DXR_UpdateAccelerationStructure(const std::nullptr_t& keyHandle)
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	DX12Rendering::TopLevelAccelerationStructure* tlas = DXR_GetOrGenerateAccelerationStructure(keyHandle);

	// Generate the TLAS resource
	//DX12Rendering::WriteLock raytraceLock(m_raytracingLock);
	m_raytracing->GenerateTLAS(tlas);
}

void DX12Renderer::DXR_SetRenderParam(DX12Rendering::dxr_renderParm_t param, const float* uniform)
{
	if (m_raytracing != nullptr)
	{
		assert("DX12Renderer::DXR_Uniform4f called with no active raytracing object.");
	}

	m_raytracing->Uniform4f(param, uniform);
}

void DX12Renderer::DXR_SetRenderParams(DX12Rendering::dxr_renderParm_t param, const float* uniform, const UINT count)
{
	if (m_raytracing != nullptr)
	{
		assert("DX12Renderer::DXR_Uniform4f called with no active raytracing object.");
	}

	for (UINT i = 0; i < count; ++i)
	{
		m_raytracing->Uniform4f(static_cast<DX12Rendering::dxr_renderParm_t>(param + i), uniform + (i * 4));
	}
}

bool DX12Renderer::DXR_CastRays()
{
	return m_raytracing->CastRays(m_frameIndex, m_viewport, m_scissorRect);
}

void DX12Renderer::DXR_DenoiseResult()
{
	// TODO
}

void DX12Renderer::DXR_GenerateResult()
{
	// TODO
}

void DX12Renderer::DXR_CopyResultToDisplay()
{
	// TODO: Get the desired resource to display
	ID3D12Resource* resource = m_raytracing->GetOutputResource();
	D3D12_RESOURCE_STATES transitionState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	CopyResourceToDisplay(resource, 0, 0, 0, 0, m_width, m_height, transitionState);
}

void DX12Renderer::CopyResourceToDisplay(ID3D12Resource* resource, UINT sx, UINT sy, UINT rx, UINT ry, UINT width, UINT height, D3D12_RESOURCE_STATES sourceState)
{
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	commandList->AddCommand([&](ID3D12GraphicsCommandList4* commandList, ID3D12CommandQueue* commandQueue)
	{
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(resource, sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commandList->ResourceBarrier(1, &transition);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
		commandList->ResourceBarrier(1, &transition);

		CD3DX12_TEXTURE_COPY_LOCATION dst(m_renderTargets[m_frameIndex].Get());
		CD3DX12_TEXTURE_COPY_LOCATION src(resource);
		CD3DX12_BOX srcBox(sx, sy, sx + width, sy + height);
		commandList->CopyTextureRegion(&dst, rx, ry, 0, &src, &srcBox);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->ResourceBarrier(1, &transition);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, sourceState);
		commandList->ResourceBarrier(1, &transition);
	});
}

#ifdef _DEBUG

void DX12Renderer::DebugAddLight(const viewLight_t& light)
{
	m_debugLights.push_back(light);
}

void DX12Renderer::DebugClearLights()
{
	m_debugLights.clear();
}

#ifdef DEBUG_IMGUI

void DX12Renderer::InitializeImGui(HWND hWnd)
{
	m_debugMode = DEBUG_LIGHTS;

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imguiSrvDescHeap)) != S_OK)
		{
			DX12Rendering::WarnMessage("Failed to create ImGui Descriptor heap.");
			return;
		}
	}

	DX12Rendering::ImGui_InitForGame(hWnd, m_device.Get(), DX12_FRAME_COUNT, m_imguiSrvDescHeap.Get());
}

void DX12Renderer::ReleaseImGui()
{
	if (m_imguiSrvDescHeap != nullptr) 
	{
		m_imguiSrvDescHeap->Release();
		m_imguiSrvDescHeap = nullptr;
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void DX12Renderer::ImGuiDebugWindows()
{
	if (m_debugMode == DEBUG_LIGHTS)
	{
		const float scaleX = 0.33f;
		const float scaleY = 0.33f;
		const float headerOffset = 25.0f;

		ImGui::SetNextWindowSize({ static_cast<float>(m_viewport.Width) * scaleX, (static_cast<float>(m_viewport.Height) * scaleY) + headerOffset });

		if (ImGui::Begin("Lighting Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::for_each(m_debugLights.cbegin(), m_debugLights.cend(), [&scaleX, &scaleY, &headerOffset](const viewLight_t& light)
			{
				ImDrawList* drawList = ImGui::GetWindowDrawList();

				if (drawList == nullptr)
				{
					return;
				}

				// Calculate the colour
				UINT32 color = 0x12345678 + light.lightDef->index;
				color = ((color << light.lightDef->index) & -1) | (color >> (32 - light.lightDef->index));

				color = color | 0x070707FF;

				const ImVec2 position = ImGui::GetWindowPos();
				const ImVec2 offset(position.x, position.y + headerOffset);
				const ImVec2 vec1((light.scissorRect.x1 * scaleX) + offset.x, (light.scissorRect.y1 * scaleY) + offset.y);
				const ImVec2 vec2((light.scissorRect.x2 * scaleX) + offset.x, (light.scissorRect.y2 * scaleY) + offset.y);

				drawList->AddRect(vec1, vec2, color);
			});
		}

		ImGui::End();
	}
	else if (m_debugMode == DEBUG_RAYTRACING)
	{
		ImGui::SetNextWindowSize({ 400, 500 });
		if (ImGui::Begin("Raytracing Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

			// Draw the base information

			if (m_raytracing != nullptr)
			{
				m_raytracing->ImGuiDebug();
			}
		}

		ImGui::End();
	}
}

#endif
#endif
