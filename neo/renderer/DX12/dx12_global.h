#ifndef __DX12_GLOBAL_H__
#define __DX12_GLOBAL_H__

#include <wrl.h>
#include <initguid.h>
#include "./d3dx12.h"
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <debugapi.h>
#include <dxcapi.h>
#include <DirectXMath.h>
#include <vector>

// Will be automatically enabled with preprocessor symbols: USE_PIX, DBG, _DEBUG, PROFILE, or PROFILE_BUILD
/*#include <pix3.h>
#include <DXProgrammableCapture.h>*/

#pragma comment (lib, "dxguid.lib")
#pragma comment (lib, "d3d12.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "dxcompiler.lib")

#define DX12_FRAME_COUNT 2

#define TEXTURE_REGISTER_COUNT 5
#define MAX_DESCRIPTOR_COUNT 7 // 2 CBV and 5 Shader Resource View
#define MAX_OBJECT_COUNT 10000
#define MAX_HEAP_INDEX_COUNT 70000

using namespace DirectX;
using namespace Microsoft::WRL;

struct Vertex
{
	XMFLOAT4 position;
	XMFLOAT4 colour;
};

struct DX12VertexBuffer
{
	ComPtr<ID3D12Resource> vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
};

struct DX12IndexBuffer
{
	ComPtr<ID3D12Resource> indexBuffer;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	UINT indexCount;
};

struct DX12CompiledShader
{
	byte* data;
	size_t size;
};

struct DX12TextureBuffer
{
	ComPtr<ID3D12Resource> textureBuffer;
	D3D12_SHADER_RESOURCE_VIEW_DESC textureView;
	D3D12_RESOURCE_STATES usageState;
	const idStr* name;
};

struct DX12JointBuffer
{
	ComPtr<ID3D12Resource> jointBuffer;
	UINT entrySizeInBytes;
};

enum eStageType {
	DEPTH_STAGE,
};

struct DX12Stage
{
	eStageType type;

	ComPtr<ID3D12Resource> tlas; // Top Level Acceleration Structure - Used for raytracing.

	UINT textureCount;
	DX12TextureBuffer* textures[TEXTURE_REGISTER_COUNT];

	// TODO: Include stage information to tell if this is for shadow testing or lighting.
};

//TODO: IMPELEMENT THIS OBJECT LIST
struct DX12Object
{
	UINT index;
	
	ID3D12PipelineState* pipelineState;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvView;
	D3D12_CONSTANT_BUFFER_VIEW_DESC jointView;
	bool includeJointView;

	DX12VertexBuffer* vertexBuffer;
	UINT vertexOffset;
	UINT vertexCount;

	DX12IndexBuffer* indexBuffer;
	UINT indexOffset;
	UINT indexCount;

	ComPtr<ID3D12Resource> blas; // Bottom Level Acceleration Structure - Used for raytracing.

	std::vector<DX12Stage> stages;
};

void DX12FailMessage(LPCSTR message);
void DX12WarnMessage(LPCSTR message);
void DX12ThrowIfFailed(HRESULT hr);
bool DX12WarnIfFailed(HRESULT hr);

#endif