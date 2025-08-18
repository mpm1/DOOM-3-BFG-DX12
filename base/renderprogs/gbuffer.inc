/*
Basic functions to enable the gbuffer.
*/

#include "global.inc"
#include "bindless_textures.inc"

#ifdef SKINNED
#include "joints.inc"
#endif

struct GBufferConstants
{
	uint bumpMapIndex;
	uint albedoIndex;
	uint specularIndex;
	uint pad;
};
ConstantBuffer<GBufferConstants> gbufferConstants : register(b2, space0);

#ifdef VERTEX_SHADER
struct VS_IN {
	float4 position : POSITION;
	float2 texcoord : TEXCOORD0;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
#ifdef SKINNED
	float4 color : COLOR0;
	float4 color2 : COLOR1; // The location of skinning
#elif  defined(USE_VELOCITY_OFFSET)
	float4 velocity : COLOR0; // For skinned objects computed on the GPU, this will contain the velocity vector.
#endif
};

struct VS_OUT {
	float4 position		: SV_POSITION;
	float4 texcoord0	: TEXCOORD0_centroid; // Bump map
	float4 texcoord1	: TEXCOORD1_centroid; // Diffuse map
	float4 texcoord2	: TEXCOORD2_centroid; // Specular map
	float3 normal		: TEXCOORD3_centroid;
	float3 tangent		: TEXCOORD4_centroid;
	float3 binormal		: TEXCOORD5_centroid;
	float4 viewPosition	: TEXCOORD6;
	float4 lastPosition	: TEXCOORD7; // Position during the last frame.
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
	matX = GetJoint(int(joint+0)) * w0;
	matY = GetJoint(int(joint+1)) * w0;
	matZ = GetJoint(int(joint+2)) * w0;

	joint = vertex.color.y * 255.1 * 3;
	matX += GetJoint(int(joint+0)) * w1;
	matY += GetJoint(int(joint+1)) * w1;
	matZ += GetJoint(int(joint+2)) * w1;

	joint = vertex.color.z * 255.1 * 3;
	matX += GetJoint(int(joint+0)) * w2;
	matY += GetJoint(int(joint+1)) * w2;
	matZ += GetJoint(int(joint+2)) * w2;

	joint = vertex.color.w * 255.1 * 3;
	matX += GetJoint(int(joint+0)) * w3;
	matY += GetJoint(int(joint+1)) * w3;
	matZ += GetJoint(int(joint+2)) * w3;

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
	result.viewPosition.x = dot4( modelPosition, sceneConstants.rpModelMatrixX );
	result.viewPosition.y = dot4( modelPosition, sceneConstants.rpModelMatrixY );
	result.viewPosition.z = dot4( modelPosition, sceneConstants.rpModelMatrixZ );
	result.viewPosition.w = dot4( modelPosition, sceneConstants.rpModelMatrixW );

	result.position.x = dot4( modelPosition, sceneConstants.rpMVPmatrixX );
	result.position.y = dot4( modelPosition, sceneConstants.rpMVPmatrixY );
	result.position.z = dot4( modelPosition, sceneConstants.rpMVPmatrixZ );
	result.position.w = dot4( modelPosition, sceneConstants.rpMVPmatrixW );

#ifdef WRITE_VELOCITY
	result.lastPosition.x = dot4( modelPosition, sceneConstants.rpPrevMVPmatrixX );
	result.lastPosition.y = dot4( modelPosition, sceneConstants.rpPrevMVPmatrixY );
	result.lastPosition.z = dot4( modelPosition, sceneConstants.rpPrevMVPmatrixZ );
	result.lastPosition.w = dot4( modelPosition, sceneConstants.rpPrevMVPmatrixW );
#endif
	
	// Generate normal space
	result.normal = applyNormalTransform(normal);
	result.tangent = applyNormalTransform(tangent);
	result.binormal = applyNormalTransform(binormal);

	float4 defaultTexCoord = float4( 0.0f, 0.5f, 0.0f, 1.0f );

	//textures 0 takes the base coordinates by the texture matrix
	result.texcoord0 = defaultTexCoord;
	result.texcoord0.x = dot4( vertex.texcoord.xy, sceneConstants.rpBumpMatrixS );
	result.texcoord0.y = dot4( vertex.texcoord.xy, sceneConstants.rpBumpMatrixT );

	//# textures 1 takes the base coordinates by the texture matrix
	result.texcoord1 = defaultTexCoord;
	result.texcoord1.x = dot4( vertex.texcoord.xy, sceneConstants.rpDiffuseMatrixS );
	result.texcoord1.y = dot4( vertex.texcoord.xy, sceneConstants.rpDiffuseMatrixT );

	//# textures 2 takes the base coordinates by the texture matrix
	result.texcoord2 = defaultTexCoord;
	result.texcoord2.x = dot4( vertex.texcoord.xy, sceneConstants.rpSpecularMatrixS );
	result.texcoord2.y = dot4( vertex.texcoord.xy, sceneConstants.rpSpecularMatrixT );
}
#endif // end VERTEX_SHADER

#ifdef PIXEL_SHADER

struct PS_IN {
	half4 position	: SV_POSITION;

	half4 texcoord0	: TEXCOORD0_centroid; // Bumpmap location
	half4 texcoord1	: TEXCOORD1_centroid; // Diffuse location
	half4 texcoord2 : TEXCOORD2_centroid; // Specular location
	
	half3 normal	: TEXCOORD3_centroid; // Normal
	half3 tangent	: TEXCOORD4_centroid; // Tangent
	half3 binormal	: TEXCOORD5_centroid; // Binormal

	float4 viewPosition : TEXCOORD6;
	half4 lastPosition	: TEXCOORD7; // Position during the last frame.
};

struct PS_OUT {
#ifdef WRITE_LOCATION_DATA
	half3 flatNormal	: SV_TARGET0;
	float3 viewDepth	: SV_TARGET1;
	half3 normal		: SV_TARGET2;
#endif
	half4 albedo		: SV_TARGET3;
	half4 specularColor	: SV_TARGET4;
#ifdef WRITE_VELOCITY
	float4 velocity		: SV_TARGET5;
#endif
};

#ifdef WRITE_VELOCITY
float4 CalculateVelocity(const float4 lastPosition, const float4 position)
{ 
	//TODO: Our velocity buffer is broken and not writing the correct data
	const float velocityScale = 8.0; // TODO: Pass in this value.
	float3 pOld = lastPosition.xyz / lastPosition.w;
	float3 pNew = position.xyz / position.w;

	float2 velocity = (pNew - pOld) * velocityScale;
	return float4(
		(velocity / max(length(velocity), 1.0)) * 0.5 + 0.5,
		1.0, 0);
}
#endif

void main( PS_IN fragment, out PS_OUT result ) {
	half4 bumpMap = SampleBindlessTexture4f(gbufferConstants.bumpMapIndex, fragment.texcoord0.xy );
	half4 YCoCG = SampleBindlessTexture4f(gbufferConstants.albedoIndex, fragment.texcoord1.xy);
	
	result.specularColor = SampleBindlessTexture4f(gbufferConstants.specularIndex, fragment.texcoord2.xy );
	
	result.albedo.rgb = ConvertYCoCgToRGB( YCoCG ); //we need the diffuse modifier constant?

#ifdef WRITE_VELOCITY
	result.velocity = CalculateVelocity(fragment.lastPosition, fragment.position);
#endif

#ifdef WRITE_LOCATION_DATA
	result.viewDepth = fragment.viewPosition.xyz;//length(fragment.viewPosition.xyz);// / fragment.viewPosition.w); // The depth going into the view space. Note, z will be negitive

	result.flatNormal = (normalize(fragment.normal) + 1.0) / 2.0f;

	half3 localNormal;
	localNormal.xy = bumpMap.wy - 0.5;
	localNormal.z = sqrt( abs( dot( localNormal.xy, localNormal.xy ) - 0.25 ) );
	localNormal = normalize( localNormal );

	float3x3 normalMatrix = {
		fragment.tangent.x, fragment.binormal.x, fragment.normal.x,
		fragment.tangent.y, fragment.binormal.y, fragment.normal.y,
		fragment.tangent.z, fragment.binormal.z, fragment.normal.z
	};
	result.normal = mul(normalMatrix, localNormal);

	result.normal = (normalize(result.normal) + 1.0) / 2.0f;
#endif
}
#endif // end PIXEL_SHADER