#pragma hdrstop

#include "../../idlib/precompiled.h"

#include "../tr_local.h"

#include <comdef.h>
#include <type_traits>

idCVar r_useRayTraycing("r_useRayTraycing", "0", CVAR_RENDERER | CVAR_BOOL, "use the raytracing system for scene generation.");

DX12Renderer dxRenderer;
extern idCommon* common;

DX12Renderer::DX12Renderer() :
	m_frameIndex(0),
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
	if (m_initialized)
	{
		return;
	}

	LoadPipeline(hWnd);
	LoadAssets();

#ifdef DEBUG_IMGUI
	InitializeImGui(hWnd);
#endif

#ifdef DEBUG_PIX
	PIXSetTargetWindow(hWnd);
#endif

	if (r_useRayTraycing.GetBool()) {
		m_raytracing = new DX12Rendering::Raytracing(m_width, m_height);
	}

	m_initialized = true;
}

void DX12Renderer::LoadPipeline(HWND hWnd) {
#if defined(DEBUG_GPU)
	{
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)))) {
			// Turn on auto-breadcrumbs and page fault reporting.
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}

		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	DX12Rendering::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	DX12Rendering::Device::InitializeDevice(factory.Get());

	DX12Rendering::GenerateRenderSurfaces();

	DX12Rendering::Commands::InitializeCommandLists();

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
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	for (UINT frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex) {
		DX12Rendering::RenderSurface& renderTarget = *DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1 + frameIndex);

		// Create RTV for each frame
		if (!renderTarget.AttachSwapchain(frameIndex, *m_swapChain.Get()))
		{
			return false;
		}

		renderTarget.Resize(m_width, m_height);
	}

	// Create the DSV
	DX12Rendering::RenderSurface* depthBuffer = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil);
	if (!depthBuffer->Resize(m_width, m_height))
	{
		return false;
	}

	return true;
}

void DX12Renderer::LoadPipelineState(D3D12_GRAPHICS_PIPELINE_STATE_DESC* psoDesc, ID3D12PipelineState** ppPipelineState) {
	assert(ppPipelineState != NULL);

	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	psoDesc->pRootSignature = m_rootSignature->GetRootSignature();

	DX12Rendering::ThrowIfFailed(device->CreateGraphicsPipelineState(psoDesc, IID_PPV_ARGS(ppPipelineState)));	
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
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	// Create the synchronization objects
	{
		m_frameFence.Allocate(device);

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
			DX12Rendering::Device::OnDeviceRemoved,
			device,
			INFINITE,
			0
		);
#endif

		// Wait for the command list to execute
		SignalNextFrame();
		WaitForPreviousFrame();
	}

	// Create Empty Root Signature
	m_rootSignature = new DX12RootSignature(device, sizeof(m_constantBuffer));

	{
		std::fill(m_activeTextures, m_activeTextures + TEXTURE_REGISTER_COUNT, static_cast<DX12Rendering::TextureBuffer*>(nullptr));
	}
}

DX12Rendering::Geometry::VertexBuffer* DX12Renderer::AllocVertexBuffer(UINT numBytes, LPCWSTR name) {
	DX12Rendering::Geometry::VertexBuffer* result = new DX12Rendering::Geometry::VertexBuffer(numBytes, name);

	return result;
}

void DX12Renderer::FreeVertexBuffer(DX12Rendering::Geometry::VertexBuffer* buffer) {
	buffer->Release();
}

DX12Rendering::Geometry::IndexBuffer* DX12Renderer::AllocIndexBuffer(UINT numBytes, LPCWSTR name) {
	DX12Rendering::Geometry::IndexBuffer* result = new DX12Rendering::Geometry::IndexBuffer(numBytes, name);

	return result;
}

void DX12Renderer::FreeIndexBuffer(DX12Rendering::Geometry::IndexBuffer* buffer) {
	buffer->Release();
}

DX12Rendering::Geometry::JointBuffer* DX12Renderer::AllocJointBuffer(UINT numBytes) {
	// Create the buffer size.
	constexpr UINT resourceAlignment = (1024 * 64) - 1; // Resource must be a multible of 64KB
	constexpr UINT entrySize = ((sizeof(float) * 4 * 404) + 255) & ~255; // (Size of float4 * maxFloatAllowed) that's 256 byts aligned.
	const UINT heapSize = (numBytes + resourceAlignment) & ~resourceAlignment;

	DX12Rendering::Geometry::JointBuffer* result = new DX12Rendering::Geometry::JointBuffer(entrySize, heapSize, L"Joint Upload Heap");

	return result;
}

void DX12Renderer::FreeJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer) {
	buffer->Release();
}

void DX12Renderer::SetJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer, UINT jointOffset) {
	D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor = m_rootSignature->SetJointDescriptorTable(buffer, jointOffset, m_frameIndex, DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT));
}

void DX12Renderer::SignalNextFrame() {
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	m_frameFence.Signal(device, commandList->GetCommandQueue());
}

void DX12Renderer::WaitForPreviousFrame() {
	m_frameFence.Wait();
}

void DX12Renderer::WaitForCopyToComplete() {
	m_copyFence.Wait();
}

void DX12Renderer::SetCommandListDefaults(const bool resetPipelineState) {
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
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

		const DX12Rendering::RenderSurface* renderTarget = GetCurrentRenderTarget();
		const DX12Rendering::RenderSurface* depthStencil = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil);

		D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[1] =
		{
			renderTarget->GetRtv()
		};

		commandList->OMSetRenderTargets(1, renderTargets, FALSE, &(depthStencil->GetDsv()));

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

	WaitForPreviousFrame();

#ifdef _DEBUG
	DebugClearLights();
#endif

	DX12Rendering::Commands::CommandListsBeginFrame();

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	DX12Rendering::CaptureEventStart(commandList, "Draw");

	{
		D3D12_RESOURCE_BARRIER transition = {};
		if (DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil)->TryTransition(D3D12_RESOURCE_STATE_DEPTH_WRITE, &transition))
		{
			commandList->CommandResourceBarrier(1, &transition);
		}
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_rootSignature->BeginFrame(m_frameIndex);
	
	m_objectIndex = 0;
	m_isDrawing = true;

	if (IsRaytracingEnabled())
	{
		DXR_UpdateBLAS();
		m_raytracing->BeginFrame();
	}

	// Reset Allocators.
	if (m_copyFence.IsFenceCompleted()) {
		//if (FAILED(m_copyCommandAllocator->Reset())) {
		//	common->Warning("Could not reset the copy command allocator.");
		//	//return;
		//}
	}

	{
		D3D12_RESOURCE_BARRIER transition = {};
		if (GetCurrentRenderTarget()->TryTransition(D3D12_RESOURCE_STATE_RENDER_TARGET, &transition))
		{
			commandList->CommandResourceBarrier(1, &transition);
		}
	}

	SetCommandListDefaults(false);
}

void DX12Renderer::EndDraw() {
	if (!m_isDrawing) {
		return;
	}

#ifdef DEBUG_IMGUI
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[1] =
	{
		GetCurrentRenderTarget()->GetRtv()
	};

	DX12Rendering::ImGui_StartFrame();
	ImGuiDebugWindows();
	DX12Rendering::ImGui_EndFrame(m_imguiSrvDescHeap.Get(), renderTargets);
#endif


	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	
	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
	{
		// present the backbuffer
		D3D12_RESOURCE_BARRIER transition = {};
		if (GetCurrentRenderTarget()->TryTransition(D3D12_RESOURCE_STATE_PRESENT, &transition))
		{
			commandList->ResourceBarrier(1, &transition);
		}
	});

	DX12Rendering::CaptureEventEnd(commandList);
	commandList->Execute();

	DX12Rendering::Commands::CommandListsEndFrame();

	m_isDrawing = false;
	
	DX12Rendering::IncrementFrameIndex();

	//After frame events
	if (IsRaytracingEnabled())
	{
		// TODO: Evaluate cleanup
		//m_raytracing->GetTLASManager()->Reset();
	}

	//common->Printf("%d heap objects registered.\n", m_cbvHeapIndex);
}

UINT DX12Renderer::StartSurfaceSettings() {
	assert(m_isDrawing);
	++m_objectIndex;

	if (m_objectIndex >= MAX_OBJECT_COUNT) {
		m_objectIndex = 0;
	}

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	return m_objectIndex;
}

bool DX12Renderer::EndSurfaceSettings() {
	// TODO: Define separate CBV for location data and Textures
	// TODO: add a check if we need to update tehCBV and Texture data.

	assert(m_isDrawing);

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	if (!DX12_ActivatePipelineState()) {
		// We cant draw the object, so return.
		return false;
	}
	
	const D3D12_CONSTANT_BUFFER_VIEW_DESC cbvView = m_rootSignature->SetCBVDescriptorTable(sizeof(m_constantBuffer), m_constantBuffer, m_objectIndex, m_frameIndex, commandList);

	// Copy the Textures
	DX12Rendering::TextureBuffer* currentTexture;
	UINT index;
	for (index = 0; index < TEXTURE_REGISTER_COUNT && (currentTexture = m_activeTextures[index]) != nullptr; ++index) {
		m_textureManager.SetTexturePixelShaderState(currentTexture);
		m_rootSignature->SetTextureRegisterIndex(index, currentTexture, m_frameIndex, commandList);
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

void DX12Renderer::DrawModel(DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffset, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount) {
	if (!IsScissorWindowValid()) {
		return;
	}

	const D3D12_VERTEX_BUFFER_VIEW& vertecies = *vertexBuffer->GetView();

	const D3D12_INDEX_BUFFER_VIEW& indecies = *indexBuffer->GetView();

	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);

	commandList->AddCommandAction([&indecies, &vertecies, &indexCount, &indexOffset, &vertexOffset](ID3D12GraphicsCommandList4* commandList)
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

		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			if (color) {
				const DX12Rendering::RenderSurface* renderTarget = GetCurrentRenderTarget();
				
				commandList->ClearRenderTargetView(renderTarget->GetRtv(), colorRGBA, 0, nullptr);
			}

			if (clearFlags > 0) {
				const DX12Rendering::RenderSurface* depthStencil = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil);

				commandList->ClearDepthStencilView(depthStencil->GetDsv(), static_cast<D3D12_CLEAR_FLAGS>(clearFlags), 1.0f, stencilValue, 0, nullptr);
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

	DX12Rendering::DestroySurfaces();

	DX12Rendering::Device::DestroyDevice();

	m_initialized = false;
}

bool DX12Renderer::SetScreenParams(UINT width, UINT height, int fullscreen)
{
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

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
	if (device && m_swapChain) {
		if (m_isDrawing) {
			WaitForPreviousFrame();

			DX12Rendering::Commands::CommandListsEndFrame();
		}

		// Resize Swapchain
		{
			// This will be updated during the swap chain.
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1)->Release();
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget2)->Release();
		}

		if (FAILED(m_swapChain->ResizeBuffers(DX12_FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, NULL))) {
			return false;
		}

		{
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1)->AttachSwapchain(0, *m_swapChain.Get());
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget2)->AttachSwapchain(1, *m_swapChain.Get());
		}

		// Resize surfaces
		{
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Diffuse)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Specular)->Resize(width, height);

			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget2)->Resize(width, height);
		}

		m_frameIndex = 0;
		/*if (!CreateBackBuffer()) {
			return false;
		}*/

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
	m_scissorRect.bottom = m_viewport.Height - y;
	m_scissorRect.top = m_scissorRect.bottom - h;

	if (m_isDrawing) {
		auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
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
			commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
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

void DX12Renderer::SetTexture(DX12Rendering::TextureBuffer* buffer) {
	m_activeTextures[m_activeTextureRegister] = buffer;
}

void DX12Renderer::DXR_ResetAccelerationStructure()
{
	
	if (!IsRaytracingEnabled()) {
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);
	
	m_raytracing->GetTLASManager()->Reset(DX12Rendering::INSTANCE_TYPE_STATIC | DX12Rendering::INSTANCE_TYPE_DYNAMIC);
	m_raytracing->GetBLASManager()->Reset();

	// TODO: what else do we need to reset.
	//m_raytracing->ResetFrame();
}

void DX12Renderer::DXR_UpdateAccelerationStructure()
{

	if (!IsRaytracingEnabled()) {
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	m_raytracing->GenerateTLAS();

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

	if (model->NumSurfaces() <= 0 || !model->ModelHasDrawingSurfaces()) {
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	const dxHandle_t index = GetHandle(&modelHandle);
	std::string blasName = std::string(model->Name());
	DX12Rendering::BottomLevelAccelerationStructure& blas = *m_raytracing->GetBLASManager()->CreateBLAS(index, std::wstring(blasName.begin(), blasName.end()).c_str());
	
	for (UINT surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
	{
		DX12Rendering::Geometry::VertexBuffer* vertexBuffer = nullptr;
		idIndexBuffer* indexBuffer = nullptr;

		const modelSurface_t& surf = *model->Surface(surfaceIndex);

		if (surf.shader->Coverage() == MC_TRANSLUCENT)
		{
			// We are not adding translucent surfaces to the trace.
			continue;
		}

		const vertCacheHandle_t& vbHandle = surf.geometry->ambientCache;
		const vertCacheHandle_t& ibHandle = surf.geometry->indexCache;

		int vertOffsetBytes;
		int indexOffsetBytes;

		// Get vertex buffer
		if (vertexCache.CacheIsStatic(vbHandle))
		{
			vertexBuffer = reinterpret_cast<DX12Rendering::Geometry::VertexBuffer*>(vertexCache.staticData.vertexBuffer.GetAPIObject());
			vertOffsetBytes = static_cast<int>(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

			indexBuffer = &vertexCache.staticData.indexBuffer;
			indexOffsetBytes = static_cast<int>(ibHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
		}
		else
		{
			continue;
			//std::vector<triIndex_t> indecies;
			//std::vector<triIndex_t>::iterator iIterator = indecies.begin();
			//std::vector<idDrawVert> vertecies;

			//for (int sIndex = 0; sIndex < model->NumSurfaces(); ++sIndex)
			//{
			//	// TODO: make separate for each material.
			//	const modelSurface_t* surf = model->Surface(sIndex);

			//	// Add all indecies
			//	iIterator = indecies.insert(iIterator, surf->geometry->indexes, surf->geometry->indexes + surf->geometry->numIndexes);

			//	// Add all vertecies
			//	vertecies.reserve(vertecies.size() + surf->geometry->numVerts);
			//	for (int vIndex = 0; vIndex < surf->geometry->numVerts; ++vIndex)
			//	{
			//		idDrawVert* vert = &surf->geometry->verts[vIndex];
			//		vertecies.push_back(*vert);
			//	}
			//}

			//m_raytracing->AddOrUpdateVertecies(device, modelHandle, reinterpret_cast<byte*>(&vertecies[0]), vertecies.size() * sizeof(idDrawVert));
			//return;
		};

		blas.AddGeometry(
			vertexBuffer,
			vertOffsetBytes,
			surf.geometry->numVerts,
			reinterpret_cast<DX12Rendering::Geometry::IndexBuffer*>(indexBuffer->GetAPIObject()),
			indexOffsetBytes,
			r_singleTriangle.GetBool() ? 3 : surf.geometry->numIndexes
		);
	}
}

void DX12Renderer::DXR_UpdateBLAS()
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	ID3D12Device5* device = DX12Rendering::Device::GetDevice();
	if (device == nullptr)
	{
		return;
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	if (UINT count = m_raytracing->GetBLASManager()->Generate() > 0)
	{
		//TODO: Move this to a more generalized execution tract.
		//commandList->Cycle();
	}

	m_raytracing->CleanUpAccelerationStructure();
}

void DX12Renderer::DXR_AddEntityToTLAS(const qhandle_t& modelHandle, const float transform[16], const DX12Rendering::ACCELERATION_INSTANCE_TYPE typesMask)
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	float transformTranspose[16];
	R_MatrixTranspose(transform, transformTranspose);

	m_raytracing->GetTLASManager()->AddInstance(modelHandle, transformTranspose, typesMask);
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
	return m_raytracing->CastRays(DX12Rendering::GetCurrentFrameIndex(), m_viewport, m_scissorRect);
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
	//renderTarget->fence.Wait();
	CopySurfaceToDisplay(DX12Rendering::eRenderSurface::RaytraceDiffuseMap, 0, 0, 0, 0, m_width / 3, m_height / 3);
}

void DX12Renderer::CopySurfaceToDisplay(DX12Rendering::eRenderSurface surfaceId, UINT sx, UINT sy, UINT rx, UINT ry, UINT width, UINT height)
{
	auto commandList = DX12Rendering::Commands::GetCommandList(DX12Rendering::Commands::DIRECT);
	DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "DX12Renderer::CopySurfaceToDisplay");

	DX12Rendering::RenderSurface& surface = *DX12Rendering::GetSurface(surfaceId);
	DX12Rendering::RenderSurface& renderTarget = *GetCurrentRenderTarget();

	commandList->AddPreFenceWait(&surface.fence); // Wait for all drawing to complete.

	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
	{
		D3D12_RESOURCE_BARRIER transition = {};
		if (surface.TryTransition(D3D12_RESOURCE_STATE_COPY_SOURCE, &transition))
		{
			commandList->ResourceBarrier(1, &transition);
		}

		if (renderTarget.TryTransition(D3D12_RESOURCE_STATE_COPY_DEST, &transition))
		{
			commandList->ResourceBarrier(1, &transition);
		}

		CD3DX12_TEXTURE_COPY_LOCATION dst(renderTarget.resource.Get());
		CD3DX12_TEXTURE_COPY_LOCATION src(surface.resource.Get());
		CD3DX12_BOX srcBox(sx, sy, sx + width, sy + height);
		commandList->CopyTextureRegion(&dst, rx, ry, 0, &src, &srcBox);

		if (renderTarget.TryTransition(D3D12_RESOURCE_STATE_RENDER_TARGET, &transition))
		{
			commandList->ResourceBarrier(1, &transition);
		}
	});

	commandList->AddPostFenceSignal(&surface.fence);
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
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	m_debugMode = DEBUG_LIGHTS;

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imguiSrvDescHeap)) != S_OK)
		{
			DX12Rendering::WarnMessage("Failed to create ImGui Descriptor heap.");
			return;
		}
	}

	DX12Rendering::ImGui_InitForGame(hWnd, device, DX12_FRAME_COUNT, m_imguiSrvDescHeap.Get());
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

		if (ImGui::Begin("Lighting Debug", NULL, ImGuiWindowFlags_NoResize)) {
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
