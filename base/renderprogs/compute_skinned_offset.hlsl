// Calculates the BLAS animations through sekleton manipulation.
#include "global.inc"
#include "joints.inc"

struct BLASVertex { // TODO: modify this to take our entire structure properly
	float3 position; //DXGI_FORMAT_R32G32B32_FLOAT
	uint texcoord; // DXGI_FORMAT_R16G16_FLOAT
	uint normal; // DXGI_FORMAT_R8G8B8A8_UNORM
	uint tangent; // DXGI_FORMAT_R8G8B8A8_UNORM
	uint color; // DXGI_FORMAT_R8G8B8A8_UNORM
	uint color2; // DXGI_FORMAT_R8G8B8A8_UNORM
};

struct ComputeBLASConstants {
	uint vertCount;
	uint vertPerThread;
	uint inputOffset;
	uint outputOffset;
};
ConstantBuffer<ComputeBLASConstants> blasConstants : register(b2, space0);

RWStructuredBuffer<BLASVertex>	vertecies_uav : register(u0);

StructuredBuffer<BLASVertex>	inputVertecies_srv : register(t0);

float4 UnpackR8G8B8A8(uint inputValue)
{
	uint4 separatedValue = uint4((inputValue >> 24) & 0xFF, (inputValue >> 16) & 0xFF, (inputValue >> 8) & 0xFF, inputValue & 0xFF);

	return float4(separatedValue) / 255.0;
}

uint PackR8G8B8A8(float4 inputValue)
{
	uint4 values = uint4(inputValue * 255.0);

	return ((values.x & 0xFF) << 24) | ((values.y & 0xFF) << 16) | ((values.z & 0xFF) << 8) | (values.w & 0xFF);
}

void Skinning(in uint startIndex, in uint endIndex, in uint outputIndex)
{
	[loop]
	for (int vertIndex = startIndex; vertIndex < endIndex; ++vertIndex)
	{
		// Cycle thorugh each vertex and set their position with relation to their joint.
		// Joint index is defined by color. and the multiplier of color2
		BLASVertex vertex = inputVertecies_srv[vertIndex];

		float3 vNormal = UnpackR8G8B8A8(vertex.normal).xyz * 2.0 - 1.0;
		float4 vTangent = UnpackR8G8B8A8(vertex.tangent) * 2.0 - 1.0;

		const float4 color = UnpackR8G8B8A8(vertex.color);
		const float4 color2 = UnpackR8G8B8A8(vertex.color2);

		const float w0 = color2.x;
		const float w1 = color2.y;
		const float w2 = color2.z;
		const float w3 = color2.w;

		float4 matX, matY, matZ;	// must be float4 for vec4
		float joint = color.x * 255.1 * 3;
		matX = GetJoint(int(joint + 0)) * w0;
		matY = GetJoint(int(joint + 1)) * w0;
		matZ = GetJoint(int(joint + 2)) * w0;

		joint = color.y * 255.1 * 3;
		matX += GetJoint(int(joint + 0)) * w1;
		matY += GetJoint(int(joint + 1)) * w1;
		matZ += GetJoint(int(joint + 2)) * w1;

		joint = color.z * 255.1 * 3;
		matX += GetJoint(int(joint + 0)) * w2;
		matY += GetJoint(int(joint + 1)) * w2;
		matZ += GetJoint(int(joint + 2)) * w2;

		joint = color.w * 255.1 * 3;
		matX += GetJoint(int(joint + 0)) * w3;
		matY += GetJoint(int(joint + 1)) * w3;
		matZ += GetJoint(int(joint + 2)) * w3;

		float3 normal;
		normal.x = dot3(matX, vNormal);
		normal.y = dot3(matY, vNormal);
		normal.z = dot3(matZ, vNormal);
		normal = normalize(normal);

		float3 tangent;
		tangent.x = dot3(matX, vTangent);
		tangent.y = dot3(matY, vTangent);
		tangent.z = dot3(matZ, vTangent);
		tangent = normalize(tangent);

		float4 modelPosition;
		float4 vertPosition = float4(vertex.position, 1.0);
		modelPosition.x = dot4(matX, vertPosition);
		modelPosition.y = dot4(matY, vertPosition);
		modelPosition.z = dot4(matZ, vertPosition);
		modelPosition.w = 1.0;

		vertex.position = modelPosition;
		vertex.normal = PackR8G8B8A8(float4(normal, 0.0));
		vertex.tangent = PackR8G8B8A8(float4(tangent, 0.0));

		vertecies_uav[outputIndex] = vertex;

		++outputIndex;
	}
}

[numthreads(GROUP_THREADS, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	const uint start = DTid.x * blasConstants.vertPerThread;
	const uint end = min(start + blasConstants.vertPerThread, blasConstants.vertCount);
	
	if (start >= blasConstants.vertCount)
	{
		// This vertex does not contain useful data.
		return;
	}

	Skinning(start + blasConstants.inputOffset, end + blasConstants.inputOffset, start + blasConstants.outputOffset);
}