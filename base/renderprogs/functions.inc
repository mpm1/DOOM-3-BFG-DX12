static float dot2( float2 a, float2 b ) { return dot( a, b ); }
static float dot3( float3 a, float3 b ) { return dot( a, b ); }
static float dot3( float3 a, float4 b ) { return dot( a, b.xyz ); }
static float dot3( float4 a, float3 b ) { return dot( a.xyz, b ); }
static float dot3( float4 a, float4 b ) { return dot( a.xyz, b.xyz ); }
static float dot4( float4 a, float4 b ) { return dot( a, b ); }
static float dot4( float2 a, float4 b ) { return dot( float4( a, 0, 1 ), b ); }

// ----------------------
// YCoCg Color Conversion
// ----------------------
static const float4 matrixRGB1toCoCg1YX = float4(  0.50,  0.0, -0.50, 0.50196078 );	// Co
static const float4 matrixRGB1toCoCg1YY = float4( -0.25,  0.5, -0.25, 0.50196078 );	// Cg
static const float4 matrixRGB1toCoCg1YZ = float4(  0.0,   0.0,  0.0,  1.0 );			// 1.0
static const float4 matrixRGB1toCoCg1YW = float4(  0.25,  0.5,  0.25, 0.0 );			// Y

static const float4 matrixCoCg1YtoRGB1X = float4(  1.0, -1.0,  0.0,        1.0 );
static const float4 matrixCoCg1YtoRGB1Y = float4(  0.0,  1.0, -0.50196078, 1.0 ); // -0.5 * 256.0 / 255.0
static const float4 matrixCoCg1YtoRGB1Z = float4( -1.0, -1.0,  1.00392156, 1.0 ); // +1.0 * 256.0 / 255.0

static half3 ConvertYCoCgToRGB( half4 YCoCg ) {
	half3 rgbColor;

	YCoCg.z = ( YCoCg.z * 31.875 ) + 1.0;			//z = z * 255.0/8.0 + 1.0
	YCoCg.z = 1.0 / YCoCg.z;
	YCoCg.xy *= YCoCg.z;
	rgbColor.x = dot4( YCoCg, matrixCoCg1YtoRGB1X );
	rgbColor.y = dot4( YCoCg, matrixCoCg1YtoRGB1Y );
	rgbColor.z = dot4( YCoCg, matrixCoCg1YtoRGB1Z );
	return rgbColor;
}

static float2 CenterScale( float2 inTC, float2 centerScale ) {
	float scaleX = centerScale.x;
	float scaleY = centerScale.y;
	float4 tc0 = float4( scaleX, 0, 0, 0.5 - ( 0.5f * scaleX ) );
	float4 tc1 = float4( 0, scaleY, 0, 0.5 - ( 0.5f * scaleY ) );

	float2 finalTC;
	finalTC.x = dot4( inTC, tc0 );
	finalTC.y = dot4( inTC, tc1 );
	return finalTC;
}

static float2 Rotate2D( float2 inTC, float2 cs ) {
	float sinValue = cs.y;
	float cosValue = cs.x;

	float4 tc0 = float4( cosValue, -sinValue, 0, ( -0.5f * cosValue ) + ( 0.5f * sinValue ) + 0.5f );
	float4 tc1 = float4( sinValue, cosValue, 0, ( -0.5f * sinValue ) + ( -0.5f * cosValue ) + 0.5f );

	float2 finalTC;
	finalTC.x = dot4( inTC, tc0 );
	finalTC.y = dot4( inTC, tc1 );
	return finalTC;
}

// better noise function available at https://github.com/ashima/webgl-noise
float rand( float2 co ) {
    return frac( sin( dot( co.xy, float2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

#define _half2( x )		half2( x )
#define _half3( x )		half3( x, x, x )
#define _half4( x )		half4( x )
#define _float2( x )	float2( x )
#define _float3( x )	float3( x )
#define _float4( x )	float4( x )

static float4 idtex2Dproj( Texture2D<float4> samp, SamplerState samplerState, half4 texCoords ) { return samp.Sample( samplerState, texCoords.xy / texCoords.w); } //TODO: old data = texCoords.xyw ); }
static float4 swizzleColor( float4 c ) { return c; }