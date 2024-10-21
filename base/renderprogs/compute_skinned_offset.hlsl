// Calculates the BLAS animations through sekleton manipulation.
#include "global.inc"
#include "joints.inc"

struct BLASVertex {
	float4 position : POSITION;
	float2 texcoord : TEXCOORD0;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float4 color : COLOR0;
	float4 color2 : COLOR1;
};

struct ComputeBLASConstants {
	uint vertCount;
	uint vertOffset;
	uint vertPerThread;
	uint pad0;
};
ConstantBuffer<ComputeBLASConstants> blasConstants_ubo : register(b2);

RWStructuredBuffer<BLASVertex>	vertecies_srv : register(u0);
StructuredBuffer<BLASVertex>	inputVertecies_srv : register(t1);

inline void Skinning(in uint startIndex, in uint endIndex)
{
	[loop]
	for (int vertIndex = startIndex; vertIndex < endIndex; ++vertIndex)
	{
		// Cycle thorugh each vertex and set their position with relation to their joint.
		// Joint index is defined by color. and the multiplier of color2
		BLASVertex vertex = inputVertecies_srv[vertIndex];

		float3 vNormal = vertex.normal.xyz * 2.0 - 1.0;
		float4 vTangent = vertex.tangent * 2.0 - 1.0;

		const float w0 = vertex.color2.x;
		const float w1 = vertex.color2.y;
		const float w2 = vertex.color2.z;
		const float w3 = vertex.color2.w;

		float4 matX, matY, matZ;	// must be float4 for vec4
		float joint = int(vertex.color.x * 255.1 * 3);
		matX = GetJoint(joint + 0) * w0;
		matY = GetJoint(joint + 1) * w0;
		matZ = GetJoint(joint + 2) * w0;

		joint = int(vertex.color.y * 255.1 * 3);
		matX += GetJoint(int(joint + 0)) * w1;
		matY += GetJoint(int(joint + 1)) * w1;
		matZ += GetJoint(int(joint + 2)) * w1;

		joint = int(vertex.color.z * 255.1 * 3);
		matX += GetJoint(int(joint + 0)) * w2;
		matY += GetJoint(int(joint + 1)) * w2;
		matZ += GetJoint(int(joint + 2)) * w2;

		joint = int(vertex.color.w * 255.1 * 3);
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
		modelPosition.x = dot4(matX, vertex.position);
		modelPosition.y = dot4(matY, vertex.position);
		modelPosition.z = dot4(matZ, vertex.position);
		modelPosition.w = 1.0;

		uint outputIndex = vertIndex;
		BLASVertex outputVertex = vertecies_srv[outputIndex];

		outputVertex.position = modelPosition;
		outputVertex.normal.xyz = normal;
		outputVertex.tangent.xyz = tangent;
	}
}

[numthreads(GROUP_THREADS, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	const uint start = DTid.x * blasConstants_ubo.vertPerThread;
	const uint end = min(start + blasConstants_ubo.vertPerThread, blasConstants_ubo.vertCount);

	if (start >= blasConstants_ubo.vertCount)
	{
		// This vertex does not contain useful data.
		return;
	}

	Skinning(start + blasConstants_ubo.vertOffset, end + blasConstants_ubo.vertOffset);
}