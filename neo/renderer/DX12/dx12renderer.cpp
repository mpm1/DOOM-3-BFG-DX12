#pragma hdrstop

#include "../../idlib/precompiled.h"

#include "../tr_local.h"
#include "../Model_local.h"

#include <comdef.h>
#include <type_traits>

extern idCVar r_swapInterval; // Used for VSync
extern idCVar r_windowWidth; // Used to calculate resize
extern idCVar r_windowHeight; // Used to calculate resize
extern idCVar r_fullscreen; // Used to calculate resize

extern idCVar r_useGli; // Use the global illumination system.

idCVar r_useRayTraycing("r_useRayTraycing", "1", CVAR_RENDERER | CVAR_BOOL, "use the raytracing system for scene generation.");
idCVar r_allLightsCastShadows("r_allLightsCastShadows", "0", CVAR_RENDERER | CVAR_BOOL, "force all lights to cast shadows in raytracing.");

DX12Renderer dxRenderer;
extern idCommon* common;

namespace
{
	static void DX12_GetShaderTextureMatrix(const float* shaderRegisters, const textureStage_t* texture, float matrix[16]) {
		// Copied from RB_GetShaderTextureMatrix
		matrix[0 * 4 + 0] = shaderRegisters[texture->matrix[0][0]];
		matrix[1 * 4 + 0] = shaderRegisters[texture->matrix[0][1]];
		matrix[2 * 4 + 0] = 0.0f;
		matrix[3 * 4 + 0] = shaderRegisters[texture->matrix[0][2]];

		matrix[0 * 4 + 1] = shaderRegisters[texture->matrix[1][0]];
		matrix[1 * 4 + 1] = shaderRegisters[texture->matrix[1][1]];
		matrix[2 * 4 + 1] = 0.0f;
		matrix[3 * 4 + 1] = shaderRegisters[texture->matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large texture values, but
		// center rotations and center scales can still generate offsets that need to be > 1
		if (matrix[3 * 4 + 0] < -40.0f || matrix[12] > 40.0f) {
			matrix[3 * 4 + 0] -= (int)matrix[3 * 4 + 0];
		}
		if (matrix[13] < -40.0f || matrix[13] > 40.0f) {
			matrix[13] -= (int)matrix[13];
		}

		matrix[0 * 4 + 2] = 0.0f;
		matrix[1 * 4 + 2] = 0.0f;
		matrix[2 * 4 + 2] = 1.0f;
		matrix[3 * 4 + 2] = 0.0f;

		matrix[0 * 4 + 3] = 0.0f;
		matrix[1 * 4 + 3] = 0.0f;
		matrix[2 * 4 + 3] = 0.0f;
		matrix[3 * 4 + 3] = 1.0f;
	}

	static void DX12_BakeTectureMatrixIntoTexgen(XMFLOAT4 lightProject[3], const float* textureMatrix)
	{
		float genMatrix[16];
		float final[16];

		genMatrix[0 * 4 + 0] = lightProject[0].x;
		genMatrix[1 * 4 + 0] = lightProject[0].y;
		genMatrix[2 * 4 + 0] = lightProject[0].z;
		genMatrix[3 * 4 + 0] = lightProject[0].w;

		genMatrix[0 * 4 + 1] = lightProject[1].x;
		genMatrix[1 * 4 + 1] = lightProject[1].y;
		genMatrix[2 * 4 + 1] = lightProject[1].z;
		genMatrix[3 * 4 + 1] = lightProject[1].w;

		genMatrix[0 * 4 + 2] = 0.0f;
		genMatrix[1 * 4 + 2] = 0.0f;
		genMatrix[2 * 4 + 2] = 0.0f;
		genMatrix[3 * 4 + 2] = 0.0f;

		genMatrix[0 * 4 + 3] = lightProject[2].x;
		genMatrix[1 * 4 + 3] = lightProject[2].y;
		genMatrix[2 * 4 + 3] = lightProject[2].z;
		genMatrix[3 * 4 + 3] = lightProject[2].w;

		R_MatrixMultiply(genMatrix, textureMatrix, final);

		lightProject[0].x = final[0 * 4 + 0];
		lightProject[0].y = final[1 * 4 + 0];
		lightProject[0].z = final[2 * 4 + 0];
		lightProject[0].w = final[3 * 4 + 0];

		lightProject[1].x = final[0 * 4 + 1];
		lightProject[1].y = final[1 * 4 + 1];
		lightProject[1].z = final[2 * 4 + 1];
		lightProject[1].w = final[3 * 4 + 1];
	}
}


DX12Renderer::DX12Renderer() :
	m_width(2),
	m_height(2),
	m_fullScreen(0),
	m_rootSignature(nullptr),
	m_computeRootSignature(nullptr),
	m_raytracing(nullptr),
	m_endFrameFence(DX12Rendering::Commands::DIRECT, 0)
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

template<>
const dxHandle_t DX12Renderer::GetHandle<idRenderModel>(const idRenderModel* renderModel)
{
	//TODO: Precache the hash or have a better generation method
	return std::hash<std::string>{}(renderModel->Name());
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

	ComPtr<IDXGIFactory6> factory;
	DX12Rendering::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	DX12Rendering::Device::InitializeDevice(factory.Get());

	DX12Rendering::GenerateRenderSurfaces();

	DX12Rendering::Commands::Initialize();

	auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT);

	// Describe and create the swap chain
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = DX12_FRAME_COUNT;
	swapChainDesc.BufferDesc.Width = m_width;
	swapChainDesc.BufferDesc.Height = m_height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO: Look into changing this for HDR
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Flags = 0; // TODO: enable when in fullscreen DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	swapChainDesc.Windowed = TRUE; //TODO: Change to option

	ComPtr<IDXGISwapChain> swapChain;
	DX12Rendering::ThrowIfFailed(factory->CreateSwapChain(commandManager->GetCommandQueue(), &swapChainDesc, &swapChain));

	DX12Rendering::ThrowIfFailed(swapChain.As(&m_swapChain));

	// Remove ALT+ENTER functionality.
	DX12Rendering::ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

	if (!CreateBackBuffer()) {
		common->FatalError("Could not initailze backbuffer.");
	}

	// Create Frame Resources
	ZeroMemory(&this->m_constantBuffer, sizeof(m_constantBuffer));

	// Create the Per Frame Dynamic 
	CreateDynamicPerFrameData();
}

void DX12Renderer::CreateDynamicPerFrameData()
{
	for (UINT frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex)
	{
		m_dynamicVertexBuffer[frameIndex] = AllocVertexBuffer(DX12_ALIGN(DYNAMIC_VERTEX_MEMORY_PER_FRAME, DYNAMIC_VERTEX_ALIGNMENT), std::wstring(L"Dynamic Vertex Data %d", frameIndex).c_str(), true);
	}
}

void DX12Renderer::DestroyDynamicPerFrameData()
{
	for (UINT frameIndex = 0; frameIndex < DX12_FRAME_COUNT; ++frameIndex)
	{
		FreeVertexBuffer(m_dynamicVertexBuffer[frameIndex]);
	}
}

void DX12Renderer::ResetDynamicPerFrameData()
{
	m_nextDynamicVertexIndex = 0;
}

void DX12Renderer::StartComputeSurfaceBones()
{
	auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE)->RequestNewCommandList();
	DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "DX12Renderer::StartComputeSurfaceBones");

	DX12Rendering::Geometry::VertexBuffer* dstBuffer = GetCurrentDynamicVertexBuffer();

	commandList->AddCommandAction([&dstBuffer](ID3D12GraphicsCommandList4* commandList)
	{
		D3D12_RESOURCE_BARRIER barrier;
		if (dstBuffer->TryTransition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &barrier))
		{
			commandList->ResourceBarrier(1, &barrier);
		}
	});
}

void DX12Renderer::EndComputeSurfaceBones()
{
	auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE)->RequestNewCommandList();
	DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "DX12Renderer::EndComputeSurfaceBones");

	DX12Rendering::Geometry::VertexBuffer* dstBuffer = GetCurrentDynamicVertexBuffer();

	commandList->AddCommandAction([&dstBuffer](ID3D12GraphicsCommandList4* commandList)
	{
		D3D12_RESOURCE_BARRIER barrier;
		if (dstBuffer->TryTransition(D3D12_RESOURCE_STATE_COMMON, &barrier))
		{
			commandList->ResourceBarrier(1, &barrier);
		}
	});
}

UINT DX12Renderer::ComputeSurfaceBones(DX12Rendering::Geometry::VertexBuffer* srcBuffer, UINT inputOffset, UINT outputOffset, UINT vertBytes, DX12Rendering::Geometry::JointBuffer* joints, UINT jointOffset)
{
	struct 	ComputeConstants
	{
		UINT vertCount;
		UINT vertPerThread;
		UINT inputOffset;
		UINT outputOffset;
	};

	constexpr UINT vertStride = sizeof(idDrawVert);
	const UINT vertIndexCount = vertBytes / vertStride;
	const UINT alignedVerts = DX12_ALIGN(vertIndexCount, 64);
	const UINT vertsPerSection = (vertIndexCount <= 64) ? 1 : (alignedVerts >> 6);

	DX12Rendering::Geometry::VertexBuffer* dstBuffer = GetCurrentDynamicVertexBuffer();

	auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE)->RequestNewCommandList();
	DX12Rendering::Commands::CommandListCycleBlock cycleBlock(commandList, "DX12Renderer::ComputeSurfaceBones");

	ID3D12PipelineState* pipelineState = m_skinnedModelShader.Get();
	assert(pipelineState != nullptr);

	const UINT objectIndex = m_computeRootSignature->RequestNewObjectIndex();

	// Create constants
	DX12Rendering::ResourceManager& resourceManager = *DX12Rendering::GetResourceManager();

	{
		// Object Constants
		ComputeConstants constants = {};
		constants.vertCount = vertIndexCount;
		constants.vertPerThread = vertsPerSection;
		constants.inputOffset = inputOffset / vertStride;
		constants.outputOffset = outputOffset / vertStride;

		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(sizeof(ComputeConstants));
		resourceManager.FillConstantBuffer(buffer, &constants);

		m_computeRootSignature->SetConstantBufferView(objectIndex, DX12Rendering::eRenderRootSignatureEntry::eSurfaceCBV, buffer);
	}

	{
		// Joint Constants
		DX12Rendering::ConstantBuffer constantBuffer = {};
		constantBuffer.bufferLocation.BufferLocation = joints->resource->GetGPUVirtualAddress() + jointOffset;
		constantBuffer.bufferLocation.SizeInBytes = *joints->GetSize();

		m_computeRootSignature->SetConstantBufferView(objectIndex, DX12Rendering::eRenderRootSignatureEntry::eJointCBV, constantBuffer);
	}

	{
		// Setup the output buffer
		m_computeRootSignature->SetUnorderedAccessView(objectIndex, DX12Rendering::eRenderRootSignatureEntry::eTesxture0SRV, dstBuffer);

		// Setup the input buffer
		m_computeRootSignature->SetShaderResourceView(objectIndex, DX12Rendering::eRenderRootSignatureEntry::eTesxture1SRV, srcBuffer);
	}

	m_computeRootSignature->SetRootDescriptorTable(objectIndex, commandList);
	commandList->CommandSetPipelineState(pipelineState);

	commandList->AddCommandAction([&dstBuffer](ID3D12GraphicsCommandList4* commandList)
	{
		{
			/*D3D12_RESOURCE_BARRIER barrier;
			if (dstBuffer->TryTransition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &barrier))
			{
				commandList->ResourceBarrier(1, &barrier);
			}*/
		}

		// TODO: Make sure we;ve setup the CBV heap correctly.

		commandList->Dispatch(64, 1, 1);

		{
			/*D3D12_RESOURCE_BARRIER barrier;
			if (dstBuffer->TryTransition(D3D12_RESOURCE_STATE_COMMON, &barrier))
			{
				commandList->ResourceBarrier(1, &barrier);
			}*/
		}
	});

	return vertBytes;
}

bool DX12Renderer::CreateBackBuffer() {
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

void DX12Renderer::SetActivePipelineState(ID3D12PipelineState* pPipelineState, DX12Rendering::Commands::CommandList& commandList) {
	if (pPipelineState != NULL)
	{ //TODO: readd this functionality on commandlists with multiple draws. && pPipelineState != m_activePipelineState) {
		m_activePipelineState = pPipelineState;

		if (m_isDrawing) {
			commandList.CommandSetPipelineState(pPipelineState);
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
		// Attach event for device removal
#ifdef DEBUG_GPU
		m_removeDeviceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_removeDeviceEvent == nullptr) {
			DX12Rendering::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT);
		commandManager->SetFenceCompleteEvent(UINT64_MAX, m_removeDeviceEvent); // This is done because all fence values are set the to  UINT64_MAX value when the device is removed.

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
	m_rootSignature = new DX12Rendering::RenderRootSignature(device);

	{
		std::fill(m_activeTextures, m_activeTextures + TEXTURE_REGISTER_COUNT, static_cast<DX12Rendering::TextureBuffer*>(nullptr));
	}

	// Create the Compute Root Signature
	m_computeRootSignature = new DX12Rendering::ComputeRootSignature(device);

	// Load compute shaders
	{
		DX12Rendering::computeShader_t* computeShader = DX12Rendering::GetComputeShader(DX12Rendering::eComputeShaders::COMPUTE_SKINNED_OFFSET);
		LoadComputePipelineState(computeShader, m_computeRootSignature, &m_skinnedModelShader);
	}
}

DX12Rendering::Geometry::VertexBuffer* DX12Renderer::AllocVertexBuffer(UINT numBytes, LPCWSTR name, const bool isGPUWritable) {
	DX12Rendering::Geometry::VertexBuffer* result = new DX12Rendering::Geometry::VertexBuffer(numBytes, name, isGPUWritable);

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

void DX12Renderer::SetJointBuffer(DX12Rendering::Geometry::JointBuffer* buffer, UINT jointOffset, DX12Rendering::Commands::CommandList* commandList) {
	DX12Rendering::ConstantBuffer constantBuffer = {};
	constantBuffer.bufferLocation.BufferLocation = buffer->resource->GetGPUVirtualAddress() + jointOffset;
	constantBuffer.bufferLocation.SizeInBytes = *buffer->GetSize();

	m_rootSignature->SetConstantBufferView(m_objectIndex, DX12Rendering::eRenderRootSignatureEntry::eJointCBV, constantBuffer);
}

void DX12Renderer::SignalNextFrame() {
	auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT);

	commandManager->InsertExecutionBreak();;
	
	const DX12Rendering::Commands::FenceValue fence = commandManager->InsertFenceSignal();
	m_endFrameFence.commandList = fence.commandList;
	m_endFrameFence.value = fence.value;

	commandManager->Execute();
}

void DX12Renderer::WaitForPreviousFrame() {
	DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT)->WaitOnFence(m_endFrameFence);
}

void DX12Renderer::SetPassDefaults(DX12Rendering::Commands::CommandList* commandList, const bool isComputeQueue)
{
	if (!isComputeQueue)
	{
		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			auto rootSignature = isComputeQueue ? nullptr : m_rootSignature->GetRootSignature();
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
		});

		EnforceRenderTargets(commandList);
	}
}

void DX12Renderer::BeginDraw(const int frameIndex) {
	if (m_isDrawing || !m_initialized) {
		return;
	}

	// Set the depth bounds.
	m_zMin = 0.0f;
	m_zMax = 1.0f;

	WaitForPreviousFrame();

	DX12Rendering::GetTextureManager()->BeginFrame(frameIndex);

	// Evaluate if we need to update or  resolution
	if (r_fullscreen.GetInteger() == 0 && // We transferred to fullscreen mode, this will be handled by R_SetNewMode
		(static_cast<UINT>(r_windowWidth.GetInteger()) != this->m_width || static_cast<UINT>(r_windowHeight.GetInteger()) != this->m_height))
	{
		//TODO: add fullscreen update
		this->SetScreenParams(r_windowWidth.GetInteger(), r_windowHeight.GetInteger(), false);
	}

	DX12Rendering::UpdateFrameIndex(m_swapChain.Get());
	ResetDynamicPerFrameData();

	//WaitForPreviousFrame();

#ifdef _DEBUG
	DebugClearLights();
#endif

	DX12Rendering::Commands::BeginFrame();

	auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT);
	auto commandList = commandManager->RequestNewCommandList();
	DX12Rendering::CaptureEventStart(commandList, "Draw");

	{
		D3D12_RESOURCE_BARRIER transition = {};
		if (DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil)->TryTransition(D3D12_RESOURCE_STATE_DEPTH_WRITE, &transition))
		{
			commandList->CommandResourceBarrier(1, &transition);
		}
	}

	m_rootSignature->BeginFrame(DX12Rendering::GetCurrentFrameIndex());
	
	m_objectIndex = 0;
	m_isDrawing = true;

	if (IsRaytracingEnabled())
	{
		DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

		m_raytracing->BeginFrame();
	}

	// Reset Allocators.
	//if (m_copyFence.IsFenceCompleted()) {
	//	//if (FAILED(m_copyCommandAllocator->Reset())) {
	//	//	common->Warning("Could not reset the copy command allocator.");
	//	//	//return;
	//	//}
	//}

	{
		ResetRenderTargets();

		D3D12_RESOURCE_BARRIER transition = {};
		if (GetOutputRenderTarget()->TryTransition(D3D12_RESOURCE_STATE_RENDER_TARGET, &transition))
		{
			commandList->CommandResourceBarrier(1, &transition);
		}
	}
	
	SetPassDefaults(commandList, false);

	commandList->Close();
}

void DX12Renderer::EndDraw() {
	if (!m_isDrawing) {
		return;
	}

#ifdef DEBUG_IMGUI
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[1] =
	{
		GetOutputRenderTarget()->GetRtv()
	};

	DX12Rendering::ImGui_StartFrame();
	ImGuiDebugWindows();
	DX12Rendering::ImGui_EndFrame(m_imguiSrvDescHeap.Get(), renderTargets);
#endif


	auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT)->RequestNewCommandList();
	
	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
	{
		// present the backbuffer
		D3D12_RESOURCE_BARRIER transition = {};
		if (GetOutputRenderTarget()->TryTransition(D3D12_RESOURCE_STATE_PRESENT, &transition))
		{
			commandList->ResourceBarrier(1, &transition);
		}
	});

	DX12Rendering::CaptureEventEnd(commandList);
	commandList->Close();

	DX12Rendering::Commands::EndFrame();

	m_isDrawing = false;
	
	//After frame events
	if (IsRaytracingEnabled())
	{
		DX12Rendering::WriteLock raytraceLock(m_raytracingLock);
		m_raytracing->EndFrame();
	}

	DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT);
	SignalNextFrame();
	//common->Printf("%d heap objects registered.\n", m_cbvHeapIndex);
}

int DX12Renderer::StartSurfaceSettings(const DX12Rendering::eSurfaceVariant variantState, const idMaterial* material, DX12Rendering::Commands::CommandList& commandList) {
	assert(m_isDrawing);

	ID3D12RootSignature* rootSignature = m_rootSignature->GetRootSignature();
	if (rootSignature != nullptr)
	{
		commandList.AddCommandAction([rootSignature](ID3D12GraphicsCommandList4* cmdList)
		{
			cmdList->SetGraphicsRootSignature(rootSignature);
		});
	}

	if (!DX12_ActivatePipelineState(variantState, material, commandList)) {
		// We cant draw the object, so return.
		return -1;
	}

	UINT stencilRef = m_stencilRef;
	CD3DX12_VIEWPORT viewport = m_viewport;
	CD3DX12_RECT scissor = m_scissorRect;
	ID3D12DescriptorHeap* heaps[2] = {
		m_rootSignature->GetCBVHeap(),
		m_rootSignature->GetSamplerHeap(),
	};

	commandList.AddCommandAction([stencilRef, viewport, scissor, heaps](ID3D12GraphicsCommandList4* commandList)
	{
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissor);

		commandList->OMSetStencilRef(stencilRef);

		commandList->SetDescriptorHeaps(2, heaps);
	});

	EnforceRenderTargets(&commandList);

	m_objectIndex = m_rootSignature->RequestNewObjectIndex();

	return m_objectIndex;
}

bool DX12Renderer::EndSurfaceSettings(void* surfaceConstants,  size_t surfaceConstantsSize, DX12Rendering::Commands::CommandList& commandList) {
	// TODO: Define separate CBV for location data and Textures
	// TODO: add a check if we need to update tehCBV and Texture data.

	assert(m_isDrawing);

	DX12Rendering::ResourceManager& resourceManager = *DX12Rendering::GetResourceManager();

	{
		// Set our constant buffer values.
		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(sizeof(m_constantBuffer));
		resourceManager.FillConstantBuffer(buffer, m_constantBuffer);
		m_rootSignature->SetConstantBufferView(m_objectIndex, DX12Rendering::eRenderRootSignatureEntry::eModelCBV, buffer);

		// TODO: split this up so we have an already existing camera buffer.
	}

	if (surfaceConstants != nullptr)
	{
		// Set our constant buffer values.
		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(surfaceConstantsSize);
		resourceManager.FillConstantBuffer(buffer, surfaceConstants);
		m_rootSignature->SetConstantBufferView(m_objectIndex, DX12Rendering::eRenderRootSignatureEntry::eSurfaceCBV, buffer);
	}

	if (surfaceConstants == nullptr)
	{
		// TODO: Remove these textues all together. For now we assume that the surfaceConstants mean that were using a constant buffer to define our structures.
		// Copy the Textures
		DX12Rendering::TextureBuffer* currentTexture;
		DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();
		DX12Rendering::TextureConstants textureConstants = {};

		UINT index;
		for (index = 0; index < TEXTURE_REGISTER_COUNT && (currentTexture = m_activeTextures[index]) != nullptr; ++index) {
			textureManager->SetTexturePixelShaderState(currentTexture, &commandList);
			m_rootSignature->SetTextureRegisterIndex(m_objectIndex, index, currentTexture, &commandList);

			textureConstants.textureIndex[index] = currentTexture->GetTextureIndex();
		}

		// Set our constant buffer values.
		DX12Rendering::ConstantBuffer buffer = resourceManager.RequestTemporyConstantBuffer(sizeof(DX12Rendering::TextureConstants));
		resourceManager.FillConstantBuffer(buffer, &textureConstants);
		m_rootSignature->SetConstantBufferView(m_objectIndex, DX12Rendering::eRenderRootSignatureEntry::eTextureCBV, buffer); 
	}

	m_rootSignature->SetRootDescriptorTable(m_objectIndex, &commandList);


	return true;
}

bool DX12Renderer::IsScissorWindowValid() {
	return m_scissorRect.right > m_scissorRect.left && m_scissorRect.bottom > m_scissorRect.top;
}

void DX12Renderer::PresentBackbuffer() {
	WaitForPreviousFrame(); // Wait for everything to catch up on the GPU.

	UINT presentProperties = 0;
	UINT syncInterval = 1;

	DXGI_PRESENT_PARAMETERS presentParams = {};
	presentParams.DirtyRectsCount = 0;

	const bool inWindow = m_fullScreen <= 0;
	if (inWindow)
	{
		if (r_swapInterval.GetInteger() == 0)
		{
			//TODO: fix no vsync
			//presentProperties |= DXGI_PRESENT_DO_NOT_WAIT;
			syncInterval = 1;
		}
		else if (r_swapInterval.GetInteger() == 1)
		{
			syncInterval = 1;
		}
	}

	DX12Rendering::ThrowIfFailed(m_swapChain->Present1(syncInterval, presentProperties, &presentParams));

	SignalNextFrame();
}

void DX12Renderer::DrawModel(DX12Rendering::Commands::CommandList& commandList, DX12Rendering::Geometry::VertexBuffer* vertexBuffer, UINT vertexOffset, DX12Rendering::Geometry::IndexBuffer* indexBuffer, UINT indexOffset, UINT indexCount, size_t vertexStrideOverride) {
	if (!IsScissorWindowValid()) {
		return;
	}

	const D3D12_VERTEX_BUFFER_VIEW& vertecies = *vertexBuffer->GetMutableView();

	const D3D12_INDEX_BUFFER_VIEW& indecies = *indexBuffer->GetView();

	FLOAT zMin = m_zMin;
	FLOAT zMax = m_zMax;

	commandList.AddCommandAction([&indecies, &vertecies, &indexCount, &indexOffset, &vertexOffset, vertexStrideOverride, zMin, zMax](ID3D12GraphicsCommandList4* commandList)
	{
		D3D12_VERTEX_BUFFER_VIEW vertCopy = vertecies;
		if (vertexStrideOverride > 0)
		{
			vertCopy.StrideInBytes = static_cast<UINT>(vertexStrideOverride);
		}

		commandList->OMSetDepthBounds(zMin, zMax);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vertCopy);
		commandList->IASetIndexBuffer(&indecies);

		// Draw the model
		commandList->DrawIndexedInstanced(indexCount, 1, indexOffset, vertexOffset, 0); // TODO: Multiply by 16 for index?
	});
}

DX12Rendering::Commands::CommandList* DX12Renderer::Clear(const bool color, const bool depth, const bool stencil, byte stencilValue, const float colorRGBA[4], DX12Rendering::Commands::CommandList* commandList) {
	if (!m_isDrawing) {
		return commandList;
	}

	uint8 clearFlags = 0;
	if (depth) {
		clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
	}

	if (stencil) {
		clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
	}

	if (color || clearFlags > 0)
	{
		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			if (color) {
				const DX12Rendering::RenderSurface* renderTarget = GetOutputRenderTarget();
				
				commandList->ClearRenderTargetView(renderTarget->GetRtv(), colorRGBA, 0, nullptr);
			}

			if (clearFlags > 0) {
				const DX12Rendering::RenderSurface* depthStencil = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil);

				commandList->ClearDepthStencilView(depthStencil->GetDsv(), static_cast<D3D12_CLEAR_FLAGS>(clearFlags), 1.0f, stencilValue, 0, nullptr);
			}
		});
	}

	return commandList;
}

void DX12Renderer::OnDestroy() {
	WaitForPreviousFrame();

#ifdef DEBUG_IMGUI
	ReleaseImGui();
#endif

	DestroyDynamicPerFrameData();

	if (m_raytracing != nullptr) {
		delete m_raytracing;
		m_raytracing = nullptr;
	}

	if (m_rootSignature != nullptr) {
		delete m_rootSignature;
		m_rootSignature = nullptr;
	}

	if (m_computeRootSignature != nullptr) {
		delete m_computeRootSignature;
		m_computeRootSignature = nullptr;
	}

	DX12Rendering::DestroySurfaces();

	DX12Rendering::DestroyResourceManager();
	DX12Rendering::Device::DestroyDevice();

	m_initialized = false;
}

bool DX12Renderer::SetScreenParams(UINT width, UINT height, int fullscreen)
{
	ID3D12Device5* device = DX12Rendering::Device::GetDevice();

	const bool updateFullscreen = m_fullScreen != fullscreen;

	m_width = width;
	m_height = height;
	m_fullScreen = fullscreen;
	m_aspectRatio = static_cast<FLOAT>(width) / static_cast<FLOAT>(height);

	// TODO: HANDLE THIS WHILE DRAWING.
	if (device && m_swapChain) {
		if (m_isDrawing) {
			WaitForPreviousFrame();

			DX12Rendering::Commands::EndFrame();
		}

		UINT swapChainFlags = 0;

		if (updateFullscreen)
		{
			if (m_fullScreen <= 0)
			{
				m_swapChain->SetFullscreenState(FALSE, NULL);
			}
			else
			{
				IDXGIOutput* pOutput;
				if (DX12Rendering::Device::GetDeviceOutput(m_fullScreen - 1, &pOutput))
				{
					m_swapChain->SetFullscreenState(TRUE, pOutput);
				}

				//swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			}
		}
		
		// Resize Swapchain
		{
			// This will be updated during the swap chain.
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1)->Release();
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget2)->Release();
		}

		if (FAILED(m_swapChain->ResizeBuffers(DX12_FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags))) {
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

			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Normal)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::FlatNormal)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Position)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Albedo)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::SpecularColor)->Resize(width, height);

			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::GlobalIllumination)->Resize(width, height);

			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget1)->Resize(width, height);
			DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RenderTarget2)->Resize(width, height);
		}
		
		/*if (!CreateBackBuffer()) {
			return false;
		}*/

		DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();

		if (textureManager->IsInitialized())
		{
			textureManager->ResizeGlobalTextures(m_width, m_height);
		}
		else
		{
			textureManager->Initialize(m_width, m_height);
		}

		if (m_raytracing != nullptr) {
			m_raytracing->Resize(m_width, m_height);
		}

		UpdateViewport(0.0f, 0.0f, width, height);
		UpdateScissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));

		if (!m_isDrawing) {
			auto commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT);

			commandManager->Execute();
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

void DX12Renderer::UpdateDepthBounds(const FLOAT minDepth, const FLOAT maxDepth) {
	if (minDepth > maxDepth)
	{
		return;
	}

	m_zMin = minDepth < 0.0f ? 0.0f : minDepth;
	m_zMax = maxDepth == 0.0f ? 1.0f : maxDepth; // If the max depth is 0, essentailly disable the depth test by setting it to 1.
}

void DX12Renderer::UpdateScissorRect(const LONG x, const LONG y, const LONG w, const LONG h) {
	m_scissorRect.left = x;
	m_scissorRect.right = m_scissorRect.left + w;

	// Note: x and y are the lower left corner  of the scissor window. We need to calculate the y location to work properly with DirectX. 
	m_scissorRect.bottom = m_viewport.Height - y;
	m_scissorRect.top = m_scissorRect.bottom - h;

	if (m_isDrawing) {
		auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT)->RequestNewCommandList();
		commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
		{
			commandList->RSSetScissorRects(1, &m_scissorRect);
		});

		commandList->Close();
	}
}

void DX12Renderer::UpdateStencilRef(UINT ref) {
	if (m_stencilRef != ref) {
		m_stencilRef = ref;

		if (m_isDrawing) {
			auto commandList = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT)->RequestNewCommandList();
			commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
			{
				commandList->OMSetStencilRef(ref);
			});

			commandList->Close();
		}
	}
}

void DX12Renderer::ReadPixels(int x, int y, int width, int height, UINT readBuffer, byte* buffer) {
	// TODO: Implement
	common->Warning("Read Pixels not yet implemented.");
}

// Texture functions
void DX12Renderer::SetActiveTextureRegister(UINT8 index) {
	if (index < TEXTURE_REGISTER_COUNT) {
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
	
	//m_raytracing->GetTLASManager()->Reset(DX12Rendering::INSTANCE_TYPE_STATIC | DX12Rendering::INSTANCE_TYPE_DYNAMIC);
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
}

void DX12Renderer::DXR_RemoveModelInBLAS(const idRenderModel* model)
{
	const dxHandle_t index = GetHandle(model);
	DXR_RemoveBLAS(index);
}

void DX12Renderer::DXR_RemoveBLAS(const dxHandle_t id)
{
	// TODO
}

UINT DX12Renderer::DXR_UpdatePendingBLAS()
{
	DX12Rendering::Commands::CommandManager* commandManager = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COMPUTE);
	DX12Rendering::Commands::CommandManagerCycleBlock cycleBlock(commandManager, "DX12Renderer::DXR_UpdatePendingBLAS");

	D3D12_RESOURCE_BARRIER barrier;
	DX12Rendering::Geometry::VertexBuffer* dynamicBuffer = GetCurrentDynamicVertexBuffer();

	if (dynamicBuffer->TryTransition(
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
		&barrier))
	{
		auto commandList = commandManager->RequestNewCommandList();

		commandList->CommandResourceBarrier(1, &barrier);
		commandList->AddPostExecuteBarrier();

		commandList->Close();
	}

	const UINT result = m_raytracing->GetBLASManager()->Generate();

	if (dynamicBuffer->TryTransition(D3D12_RESOURCE_STATE_COMMON, &barrier))
	{
		auto commandList = commandManager->RequestNewCommandList();

		commandList->AddPreExecuteBarrier();
		commandList->CommandResourceBarrier(1, &barrier);

		commandList->Close();
	}

	return result;
}

DX12Rendering::BottomLevelAccelerationStructure* DX12Renderer::DXR_UpdateBLAS(const dxHandle_t id, const char* name, const bool isStatic, const size_t surfaceCount, DX12Rendering::RaytracingGeometryArgument* arguments)
{
	if (!IsRaytracingEnabled()) {
		return nullptr;
	}
	
	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	std::string blasName = std::string(name);

	DX12Rendering::BottomLevelAccelerationStructure* blas = m_raytracing->GetBLASManager()->CreateBLAS(id, isStatic, true, std::wstring(blasName.begin(), blasName.end()).c_str());

	if (blas == nullptr)
	{
		return nullptr;
	}

	for (UINT surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
	{
		DX12Rendering::Geometry::VertexBuffer* vertexBuffer = nullptr;
		idIndexBuffer* indexBuffer = nullptr;

		DX12Rendering::RaytracingGeometryArgument& geometry = arguments[surfaceIndex];

		const vertCacheHandle_t& vbHandle = geometry.vertexHandle;
		const vertCacheHandle_t& ibHandle = geometry.indexHandle;

		int vertOffsetBytes = 0;
		int indexOffsetBytes = 0;

		// Get vertex buffer
		if (vertexCache.CacheIsStatic(vbHandle))
		{
			vertexBuffer = static_cast<DX12Rendering::Geometry::VertexBuffer*>(vertexCache.staticData.vertexBuffer.GetAPIObject());
			vertOffsetBytes = (int)(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

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

		// If dynamic use the precalculated bones
		if (!isStatic)
		{
			vertexBuffer = GetCurrentDynamicVertexBuffer();
			vertOffsetBytes = geometry.vertexOffset;
		}


		blas->AddGeometry(
			vertexBuffer,
			vertOffsetBytes,
			geometry.vertCounts,
			reinterpret_cast<DX12Rendering::Geometry::IndexBuffer*>(indexBuffer->GetAPIObject()),
			indexOffsetBytes,
			geometry.indexCounts,
			geometry.jointsHandle
		);
	}

	return blas;
}

dxHandle_t DX12Renderer::DXR_GetBLASHandle(const idRenderModel* model)
{
	dxHandle_t handle = GetHandle(model);

	return handle;
}

bool DX12Renderer::DXR_BLASExists(const dxHandle_t id)
{
	return m_raytracing->GetBLASManager()->GetBLAS(id) != nullptr;
}

DX12Rendering::BottomLevelAccelerationStructure* DX12Renderer::DXR_UpdateModelInBLAS(const idRenderModel* model)
{
	if (!IsRaytracingEnabled() || model == nullptr) {
		return nullptr;
	}

	// If an object can cast shadows or receive shadows, we will count it as having Opaque surfaces.
	if (!model->ModelHasShadowCastingSurfaces() && !model->ModelHasInteractingSurfaces())
	{
		return nullptr;
	}

	if (model->NumSurfaces() <= 0 || !model->ModelHasDrawingSurfaces())
	{
		return nullptr;
	}

	bool isStatic = true;
	if (model->GetJoints() != NULL) 
	{
		isStatic = false; // We will add the joints per surface.
	}

	DX12Rendering::WriteLock raytraceLock(m_raytracingLock);

	const dxHandle_t index = GetHandle(model);
	std::vector<DX12Rendering::RaytracingGeometryArgument> geometry = {};
	geometry.reserve(model->NumSurfaces());

	for (UINT surfaceIndex = 0; surfaceIndex < static_cast<UINT>(model->NumSurfaces()); ++surfaceIndex)
	{
		const modelSurface_t& surf = *model->Surface(surfaceIndex);
		if (surf.shader->Coverage() == MC_TRANSLUCENT)
		{
			// We are not adding translucent surfaces to the trace.
			continue;
		}

		if (!surf.shader->ReceivesLighting() && !surf.shader->LightCastsShadows()) // We're assuming all light receiving objects cast shadows in this model.
		{
			// If we dont cast shadows, drop it.
			continue;
		}

		if (surf.shader->IsPortalSky())
		{
			// No point in casting against the sky.
			continue;
		}

		DX12Rendering::RaytracingGeometryArgument geo = {};
		geo.meshIndex = surfaceIndex;
		geo.jointsHandle = 0;

		geo.vertexHandle = surf.geometry->ambientCache;
		geo.vertCounts = surf.geometry->numVerts;

		geo.indexHandle = surf.geometry->indexCache;
		geo.indexCounts = surf.geometry->numIndexes;


		// Generate the joint handle
		if (!isStatic || surf.geometry->staticModelWithJoints != NULL)
		{
			// Code taken from tr_frontend_addmodels.cpp R_SetupDrawSurgJoints
			idRenderModelStatic* jointModel = surf.geometry->staticModelWithJoints;
			assert(jointModel->jointsInverted != NULL);

			if (!vertexCache.CacheIsCurrent(jointModel->jointsInvertedBuffer)) {
				const int alignment = glConfig.uniformBufferOffsetAlignment;
				jointModel->jointsInvertedBuffer = vertexCache.AllocJoint(jointModel->jointsInverted, ALIGN(jointModel->numInvertedJoints * sizeof(idJointMat), alignment));
			}
			geo.jointsHandle = jointModel->jointsInvertedBuffer;
		}

		geometry.emplace_back(geo);
	}

	return DXR_UpdateBLAS(index, model->Name(), isStatic, geometry.size(), geometry.data());
}

void DX12Renderer::DXR_AddModelBLASToTLAS(const uint entityIndex, const idRenderModel& model, const float transform[16], const DX12Rendering::ACCELERATION_INSTANCE_TYPE typesMask, const DX12Rendering::ACCELLERATION_INSTANCE_MASK instanceMask)
{
	dxHandle_t modelHandle = GetHandle(&model);

	DXR_AddBLASToTLAS(entityIndex, modelHandle, transform, typesMask, instanceMask);
}

void DX12Renderer::DXR_AddBLASToTLAS(const uint entityIndex, const dxHandle_t id, const float transform[16], const DX12Rendering::ACCELERATION_INSTANCE_TYPE typesMask, const DX12Rendering::ACCELLERATION_INSTANCE_MASK instanceMask)
{
	if (!IsRaytracingEnabled()) {
		return;
	}

	dxHandle_t instanceHandle = static_cast<dxHandle_t>(entityIndex);

	m_raytracing->GetTLASManager()->AddInstance(instanceHandle, id, transform, typesMask, instanceMask);
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
		m_raytracing->Uniform4f(param + i, uniform + (i * 4));
	}
}

bool DX12Renderer::DXR_CastRays()
{
	if (r_useGli.GetBool())
	{
		m_raytracing->CastGlobalIlluminationRays(DX12Rendering::GetCurrentFrameIndex(), m_viewport, m_scissorRect);
	}

	const DX12Rendering::Commands::FenceValue drawFence = m_raytracing->CastShadowRays(DX12Rendering::GetCurrentFrameIndex(), m_viewport, m_scissorRect);
	const bool result = drawFence.value > 0;

	if (result)
	{
		// Copy the raytraced shadow data
		DX12Rendering::TextureManager* textureManager = DX12Rendering::GetTextureManager();
		DX12Rendering::TextureBuffer* lightTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SHADOWMAP);

		DX12Rendering::RenderSurface* surface = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::RaytraceShadowMask);

		DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->InsertFenceWait(drawFence);

		surface->CopySurfaceToTexture(lightTexture, textureManager);

		// Copy the diffuse data
		auto diffuseMap = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Diffuse);
		DX12Rendering::TextureBuffer* diffuseTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_DIFFUSE);
		diffuseMap->CopySurfaceToTexture(diffuseTexture, textureManager);

		// Copy the specular data
		auto specularMap = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::Specular);
		DX12Rendering::TextureBuffer* specularTexture = textureManager->GetGlobalTexture(DX12Rendering::eGlobalTexture::RAYTRACED_SPECULAR);
		specularMap->CopySurfaceToTexture(specularTexture, textureManager);

		// Wait for all copies to complete.
		DX12Rendering::Commands::FenceValue fenceValue = DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->InsertFenceSignal();
		DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::COPY)->Execute();

		DX12Rendering::Commands::GetCommandManager(DX12Rendering::Commands::DIRECT)->InsertFenceWait(fenceValue);
	}

	return result;
}

void DX12Renderer::DXR_SetupLights(const viewLight_t* viewLights, const float* worldMatrix)
{
	if (!IsRaytracingEnabled()) {
		return;
	}
	
	m_raytracing->ResetLightList();
	UINT lightIndex = 0;

	for (const viewLight_t* vLight = viewLights; vLight != NULL; vLight = vLight->next) {
		// some rare lights have multiple animating stages, loop over them outside the surface list
		const idMaterial* lightShader = vLight->lightShader;
		const float* lightRegs = vLight->shaderRegisters;

		DX12Rendering::DXR_LIGHT_TYPE lightType = DX12Rendering::DXR_LIGHT_TYPE::DXR_LIGHT_TYPE_POINT; // Default light type.

		const bool isAmbientLight = lightShader->IsAmbientLight(); // TODO: add as flag.
		if (isAmbientLight)
		{
			lightType = DX12Rendering::DXR_LIGHT_TYPE::DXR_LIGHT_TYPE_AMBIENT;
		}

		if (lightShader->IsFogLight())
		{
			// Point and ambient can be fog lights.
			lightType |= DX12Rendering::DXR_LIGHT_TYPE::DXR_LIGHT_TYPE_FOG;
		}

		UINT shadowMask = vLight->shadowMask;

		const XMFLOAT4 location(
			vLight->globalLightOrigin.x,
			vLight->globalLightOrigin.y,
			vLight->globalLightOrigin.z,
			1.0f);

		const XMFLOAT4 scissor(
			vLight->scissorRect.x1 + m_viewport.TopLeftX, // left
			vLight->scissorRect.y1 + m_viewport.TopLeftY, // top
			vLight->scissorRect.x2 + m_viewport.TopLeftX, //right
			vLight->scissorRect.y2 + m_viewport.TopLeftY); //bottom

		const float lightScale = r_lightScale.GetFloat();

		for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++) 
		{
			const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);

			// ignore stages that fail the condition
			if (!lightRegs[lightStage->conditionRegister]) {
				continue;
			}

			const XMFLOAT4 lightColor(
				lightScale * lightRegs[lightStage->color.registers[0]],
				lightScale * lightRegs[lightStage->color.registers[1]],
				lightScale * lightRegs[lightStage->color.registers[2]],
				lightRegs[lightStage->color.registers[3]]);
			
			// apply the world-global overbright and the 2x factor for specular
			const XMFLOAT4 diffuseColor = lightColor;
			//const XMFLOAT4 specularColor = lightColor * 2.0f;

			// transform the light project into model local space
			XMFLOAT4 lightProjection[4];
			for (int i = 0; i < 4; i++) {
				memcpy(&lightProjection[i], vLight->lightProject[i].ToFloatPtr(), sizeof(float) * 4);
			}

			// optionally multiply the local light projection by the light texture matrix
			if (lightStage->texture.hasMatrix) {
				float lightTextureMatrix[16];
				DX12_GetShaderTextureMatrix(lightRegs, &lightStage->texture, lightTextureMatrix);
				DX12_BakeTectureMatrixIntoTexgen(lightProjection, lightTextureMatrix);
			}

			const bool castsShadows = r_allLightsCastShadows.GetBool() || vLight->castsShadows;

			if (!m_raytracing->AddLight(vLight->sceneIndex, lightType,
				static_cast<const DX12Rendering::TextureBuffer*>(vLight->falloffImage->Bindless()), 
				static_cast<const DX12Rendering::TextureBuffer*>(lightStage->texture.image->Bindless()), 
				vLight->shadowMask, location, lightColor, lightProjection, scissor, castsShadows))
			{
				// Could not add the raytraced light
				shadowMask = 0;
			}

			++lightIndex;
		}
	}
}

void DX12Renderer::DXR_DenoiseResult()
{
	// TODO
}

void DX12Renderer::ResetRenderTargets() 
{ 
	m_renderTargets[0] = GetOutputRenderTarget(); 
	m_activeRenderTargets = 1; 
}

void DX12Renderer::EnforceRenderTargets(DX12Rendering::Commands::CommandList* commandList)
{
	assert(commandList != nullptr);

	commandList->AddCommandAction([&](ID3D12GraphicsCommandList4* commandList)
	{
		UINT totalRenderTargets = 0;
		const DX12Rendering::RenderSurface** renderTargets = GetCurrentRenderTargets(totalRenderTargets);

		const DX12Rendering::RenderSurface* depthStencil = DX12Rendering::GetSurface(DX12Rendering::eRenderSurface::DepthStencil);

		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderRtvs;
		renderRtvs.reserve(totalRenderTargets);
		for (uint index = 0; index < totalRenderTargets; ++index)
		{
			renderRtvs.push_back(renderTargets[index]->GetRtv());
		}

		commandList->OMSetRenderTargets(totalRenderTargets, renderRtvs.data(), FALSE, &(depthStencil->GetDsv()));
	});
}

void DX12Renderer::SetRenderTargets(const DX12Rendering::eRenderSurface* surfaces, const UINT count)
{
	assert(count <= MAX_RENDER_TARGETS);
	
	m_activeRenderTargets = count;
	for (UINT index = 0; index < count; ++index)
	{
		m_renderTargets[index] = DX12Rendering::GetSurface(surfaces[index]);
	}
}

const DX12Rendering::RenderSurface** DX12Renderer::GetCurrentRenderTargets(UINT& count)
{ 
	count = m_activeRenderTargets; 
	return (const DX12Rendering::RenderSurface**)m_renderTargets;
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

	m_debugMode = DEBUG_UNKNOWN;

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
	else if (m_debugMode == DEBUG_RAYTRACING_SHADOWMAP)
	{
		const float scaleX = 0.33f;
		const float scaleY = 0.33f;
		const float headerOffset = 25.0f;

		ImVec2 imageSize(static_cast<float>(m_viewport.Width) * scaleX, static_cast<float>(m_viewport.Height) * scaleY);

		ImGui::SetNextWindowSize({ static_cast<float>(m_viewport.Width) * scaleX, (static_cast<float>(m_viewport.Height) * scaleY) + headerOffset });

		//if (ImGui::Begin("Raytraced Shadowmap", NULL, ImGuiWindowFlags_NoResize)) {
		//	DX12Rendering::TextureBuffer* lightTexture = m_textureManager.GetGlobalTexture(DX12Rendering::eGlobalTexture::DEPTH_TEXTURE);
		//	m_rootSignature->SetTextureRegisterIndex(0, lightTexture, nullptr); //TODO: Create a static location for these global textures. Maybe make a location so texures dont need to readd itself

		//	ImGui::Text("GPU handle = %p", lightTexture->GetGPUDescriptorHandle().ptr);
		//	ImGui::Image((ImTextureID)lightTexture->GetGPUDescriptorHandle().ptr, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 1));
		//}

		ImGui::End();
	}
}

#endif
#endif
