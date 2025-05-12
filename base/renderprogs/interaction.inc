/*
Basic functions to use the interaction shader.
*/

// TODO: Finish this. it's currently uneeded for raytracing.

#include "global.inc"

#ifdef SKINNED
#include "joints.inc"
#endif

#ifdef VERTEX_SHADER
struct VS_IN {
	float4 position : POSITION;
	float2 texcoord : TEXCOORD0;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float4 color : COLOR0;
#ifdef SKINNED
	float4 color2 : COLOR1;
#endif
};

struct VS_OUT {
	float4 position		: SV_POSITION;
	float4 texcoord0	: TEXCOORD0_centroid;
	float4 texcoord1	: TEXCOORD1_centroid;
	float4 texcoord2	: TEXCOORD2_centroid;
	float4 texcoord3	: TEXCOORD3_centroid;
	float4 texcoord4	: TEXCOORD4_centroid;
	float4 texcoord5	: TEXCOORD5_centroid;
	float4 texcoord6	: TEXCOORD6_centroid;
	float4 color		: COLOR0;
};

void main( VS_IN vertex, out VS_OUT result ) {

	float3 vNormal = vertex.normal.xyz * 2.0 - 1.0;
	float4 vTangent = vertex.tangent * 2.0 - 1.0;
	float3 vBinormal = cross( vNormal.xyz, vTangent.xyz ) * vTangent.w;

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

	float4 modelPosition;
	modelPosition.x = dot4( matX, vertex.position );
	modelPosition.y = dot4( matY, vertex.position );
	modelPosition.z = dot4( matZ, vertex.position );
	modelPosition.w = 1.0;
#else 
	float3 normal = vNormal;
	float3 tangent = vTangent.xyz;
	float3 binormal = vBinormal;

	float4 modelPosition = vertex.position;
#endif

	result.position.x = dot4( modelPosition, sceneConstants.rpMVPmatrixX );
	result.position.y = dot4( modelPosition, sceneConstants.rpMVPmatrixY );
	result.position.z = dot4( modelPosition, sceneConstants.rpMVPmatrixZ );
	result.position.w = dot4( modelPosition, sceneConstants.rpMVPmatrixW );

	float4 defaultTexCoord = float4( 0.0f, 0.5f, 0.0f, 1.0f );

	//calculate vector to light in R0
	float4 toLight = sceneConstants.rpLocalLightOrigin - vertex.position;

	//result.texcoord0
	result.texcoord0.x = dot3( tangent, toLight );
	result.texcoord0.y = dot3( tangent, toLight );
	result.texcoord0.z = dot3( tangent, toLight );
	result.texcoord0.w = 1.0f;

	//textures 1 takes the base coordinates by the texture matrix
	result.texcoord1 = defaultTexCoord;
	result.texcoord1.x = dot4( vertex.texcoord.xy, sceneConstants.rpBumpMatrixS );
	result.texcoord1.y = dot4( vertex.texcoord.xy, sceneConstants.rpBumpMatrixT );

	//# texture 2 has one texgen
	result.texcoord2 = defaultTexCoord;
	result.texcoord2.x = dot4( vertex.position, sceneConstants.rpLightFalloffS );

	//# texture 3 has three texgens
	result.texcoord3.x = dot4( vertex.position, sceneConstants.rpLightProjectionS );
	result.texcoord3.y = dot4( vertex.position, sceneConstants.rpLightProjectionT );
	result.texcoord3.z = 0.0f;
	result.texcoord3.w = dot4( vertex.position, sceneConstants.rpLightProjectionQ );

	//# textures 4 takes the base coordinates by the texture matrix
	result.texcoord4 = defaultTexCoord;
	result.texcoord4.x = dot4( vertex.texcoord.xy, sceneConstants.rpDiffuseMatrixS );
	result.texcoord4.y = dot4( vertex.texcoord.xy, sceneConstants.rpDiffuseMatrixT );

	//# textures 5 takes the base coordinates by the texture matrix
	result.texcoord5 = defaultTexCoord;
	result.texcoord5.x = dot4( vertex.texcoord.xy, sceneConstants.rpSpecularMatrixS );
	result.texcoord5.y = dot4( vertex.texcoord.xy, sceneConstants.rpSpecularMatrixT );

	//# texture 6's texcoords will be the halfangle in texture space

	//# calculate normalized vector to light in R0
	toLight = normalize( toLight );

	//# calculate normalized vector to viewer in R1
	float4 toView = normalize( sceneConstants.rpLocalViewOrigin - modelPosition );
	
	//# add together to become the half angle vector in object space (non-normalized)
	float4 halfAngleVector = toLight + toView;

	//# put into texture space
	result.texcoord6.x = dot3( tangent, halfAngleVector );
	result.texcoord6.y = dot3( binormal, halfAngleVector );
	result.texcoord6.z = dot3( normal, halfAngleVector );
	result.texcoord6.w = 1.0f;

#ifdef SKINNED
	// for joint transformation of the tangent space, we use color and
	// color2 for weighting information, so hopefully there aren't any
	// effects that need vertex color...
	result.color = float4( 1.0f, 1.0f, 1.0f, 1.0f );
#else
	//# generate the vertex color, which can be 1.0, color, or 1.0 - color
	//# for 1.0 : env[16] = 0, env[17] = 1
	//# for color : env[16] = 1, env[17] = 0
	//# for 1.0-color : env[16] = -1, env[17] = 1	
	result.color = ( swizzleColor( vertex.color ) * sceneConstants.rpVertexColorModulate ) + sceneConstants.rpVertexColorAdd;
#endif
}

#endif