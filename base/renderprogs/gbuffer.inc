/*
Basic functions to enable the gbuffer.
*/

#include "global.inc"

#ifdef VERTEX_SHADER
struct VS_IN {
	float4 position : POSITION;
	float2 texcoord : TEXCOORD0;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float4 color : COLOR0;
#ifdef SKINNED
	float4 color2 : COLOR1;
#endif // end SKINNED
};

struct VS_OUT {
	float4 position		: SV_POSITION;
	float4 texcoord0	: TEXCOORD0_centroid;
	float3 normal		: TEXCOORD1_centroid;
	float3 tangent		: TEXCOORD2_centroid;
	float3 binormal		: TEXCOORD3_centroid;
	float4 viewPosition	: TEXCOORD4;
};

float3 applyNormalTransform(float3 inVector)
{
	float4 result;

	float4 checkVector = float4(inVector, 1.0);

	result.x = dot4(checkVector, sceneConstants.rpNormalMatrixX);
	result.y = dot4(checkVector, sceneConstants.rpNormalMatrixY);
	result.z = dot4(checkVector, sceneConstants.rpNormalMatrixZ);
	result.w = dot4(checkVector, sceneConstants.rpNormalMatrixW);

	//result *= result.w;

	return normalize(result.xyz);
}

void main( VS_IN vertex, out VS_OUT result ) {
	float3 vNormal = vertex.normal.xyz * 2.0 - 1.0;
	float4 vTangent = vertex.tangent * 2.0 - 1.0;
	float3 vBinormal = cross( vNormal.xyz, vTangent.xyz ) * vTangent.w;

	float4 modelPosition;

#ifdef SKINNED
	//--------------------------------------------------------------
	// GPU transformation of the normal / binormal / bitangent
	//
	// multiplying with 255.1 give us the same result and is faster than floor( w * 255 + 0.5 )
	//--------------------------------------------------------------
	const float w0 = vertex.color2.x;
	const float w1 = vertex.color2.y;
	const float w2 = vertex.color2.z;
	const float w3 = vertex.color2.w;

	float4 matX, matY, matZ;	// must be float4 for vec4
	float joint = vertex.color.x * 255.1 * 3;
	matX = matrices_ubo.matrices[int(joint+0)] * w0;
	matY = matrices_ubo.matrices[int(joint+1)] * w0;
	matZ = matrices_ubo.matrices[int(joint+2)] * w0;

	joint = vertex.color.y * 255.1 * 3;
	matX += matrices_ubo.matrices[int(joint+0)] * w1;
	matY += matrices_ubo.matrices[int(joint+1)] * w1;
	matZ += matrices_ubo.matrices[int(joint+2)] * w1;

	joint = vertex.color.z * 255.1 * 3;
	matX += matrices_ubo.matrices[int(joint+0)] * w2;
	matY += matrices_ubo.matrices[int(joint+1)] * w2;
	matZ += matrices_ubo.matrices[int(joint+2)] * w2;

	joint = vertex.color.w * 255.1 * 3;
	matX += matrices_ubo.matrices[int(joint+0)] * w3;
	matY += matrices_ubo.matrices[int(joint+1)] * w3;
	matZ += matrices_ubo.matrices[int(joint+2)] * w3;

	float3 normal;
	normal.x = dot3( matX, vNormal );
	normal.y = dot3( matY, vNormal );
	normal.z = dot3( matZ, vNormal );
	normal = normalize( normal );

	float3 tangent;
	tangent.x = dot3( matX, vTangent );
	tangent.y = dot3( matY, vTangent );
	tangent.z = dot3( matZ, vTangent );
	tangent = normalize( tangent );

	float3 binormal;
	binormal.x = dot3( matX, vBinormal );
	binormal.y = dot3( matY, vBinormal );
	binormal.z = dot3( matZ, vBinormal );
	binormal = normalize( binormal );

	modelPosition.x = dot4( matX, vertex.position );
	modelPosition.y = dot4( matY, vertex.position );
	modelPosition.z = dot4( matZ, vertex.position );
	modelPosition.w = 1.0;
#else
	modelPosition = vertex.position;

	float3 normal = vNormal.xyz;
	float3 tangent = vTangent.xyz;
	float3 binormal = vBinormal.xyz;
#endif // end SKINNED

	// Calculate the fragment position
	result.viewPosition.x = dot4( modelPosition, sceneConstants.rpModelViewMatrixX );
	result.viewPosition.y = dot4( modelPosition, sceneConstants.rpModelViewMatrixY );
	result.viewPosition.z = dot4( modelPosition, sceneConstants.rpModelViewMatrixZ );
	result.viewPosition.w = dot4( modelPosition, sceneConstants.rpModelViewMatrixW );

	result.position.x = dot4( modelPosition, sceneConstants.rpMVPmatrixX );
	result.position.y = dot4( modelPosition, sceneConstants.rpMVPmatrixY );
	result.position.z = dot4( modelPosition, sceneConstants.rpMVPmatrixZ );
	result.position.w = dot4( modelPosition, sceneConstants.rpMVPmatrixW );
	
	// Generate normal space
	result.normal = applyNormalTransform(normal);
	result.tangent = applyNormalTransform(tangent);
	result.binormal = applyNormalTransform(binormal);

	float4 defaultTexCoord = float4( 0.0f, 0.5f, 0.0f, 1.0f );

	//textures 0 takes the base coordinates by the texture matrix
	result.texcoord0 = defaultTexCoord;
	result.texcoord0.x = dot4( vertex.texcoord.xy, sceneConstants.rpBumpMatrixS );
	result.texcoord0.y = dot4( vertex.texcoord.xy, sceneConstants.rpBumpMatrixT );
}
#endif // end VERTEX_SHADER

#ifdef PIXEL_SHADER
uniform Texture2D<float4>	samp0 : register(t0); // texture 1 is the per-surface bump map

struct PS_IN {
	half4 position	: SV_POSITION;
	half4 texcoord0	: TEXCOORD0_centroid; // Bumpmap location
	half3 normal	: TEXCOORD1_centroid; // Normal
	half3 tangent	: TEXCOORD2_centroid; // Tangent
	half3 binormal	: TEXCOORD3_centroid; // Binormal

	float4 viewPosition : TEXCOORD4;
};

struct PS_OUT {
	half3 normal	: SV_TARGET0;
	float viewDepth	: SV_TARGET1;
};

void main( PS_IN fragment, out PS_OUT result ) {
	result.viewDepth = length(fragment.viewPosition.xyz);// / fragment.viewPosition.w); // The depth going into the view space. Note, z will be negitive

	half4 bumpMap =	samp0.Sample(baseSampler, fragment.texcoord0.xy );

	half3 localNormal;
	localNormal.xy = bumpMap.wy - 0.5;
	localNormal.z = sqrt( abs( dot( localNormal.xy, localNormal.xy ) - 0.25 ) );
	localNormal = normalize( localNormal );

	result.normal = fragment.normal;
	/*result.normal.x = dot3(fragment.tangent, localNormal);
	result.normal.y = dot3(fragment.binormal, localNormal);
	result.normal.z = dot3(fragment.normal, localNormal);*/

	result.normal = (normalize(result.normal) + 1.0) / 2.0f;
}
#endif // end PIXEL_SHADER