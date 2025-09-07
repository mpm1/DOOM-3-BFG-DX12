#define EPSILON 0.0000001
#define PI 3.14159265358979323846

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

static half3 ConvertDiffuseToAlbedo(in half3 rgbDiffuse)
{
	half3 albedoColor;
	float invertLumminance = 1.0 - saturate(0.299 * rgbDiffuse.r + 0.587 * rgbDiffuse.g + 0.114 * rgbDiffuse.b); //TODO: this works ok, but we need to keep finer details in the darks. maybe a cavity texture.
	float L2 = 2.0 * invertLumminance;

	if(invertLumminance < 0.5)
	{
		albedoColor = (rgbDiffuse * L2) + pow(rgbDiffuse, 2.0) * (1.0 - L2);
	}
	else
	{
		albedoColor = (2.0 * rgbDiffuse * (1.0 - invertLumminance)) + (sqrt(rgbDiffuse) * (2.0 * invertLumminance - 1.0));
	}

	//albedoColor = ((1.0 - L2) * pow(rgbDiffuse, 2.0)) + (L2 * rgbDiffuse);

	return albedoColor;
}

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

// Temp random function. found here: https://thebookofshaders.com/10/
float rand1(float2 st, float2 dotDir) {
    return frac(sin(dot(st.xy, dotDir)) * 43758.5453123);
}

// better noise function available at https://github.com/ashima/webgl-noise
float rand( float2 co ) {
    return rand1( co.xy, float2( 12.9898, 78.233 ) );
}

float3 rand3(float3 st) {
    return float3(
		rand1(st.xy, float2(12.9898, 78.233)),
		rand1(st.yz, float2(12.9898, 78.233)), //rand1(st, float2(39.346, 11.135)),
		rand1(st.zx, float2(12.9898, 78.233)) //rand1(st, float2(73.156, 52.235))
	);
}

// Generates a random point on a unit sphere centered at 0.
inline float3 RandomSphericalPoint(float3 seed)
{
	return normalize((rand3(seed) * 2.0) - 1.0);
}

// Generates a random point on a hemisphere aligned with the normal vector. Centered at 0.
float RandomHemisphericalPoint(float3 normal, float3 seed)
{
	float3 spherePoint = RandomSphericalPoint(seed);

	return dot(normal, spherePoint) < 0 ? -spherePoint : spherePoint;
}

#define _half2( x )		half2( x, x )
#define _half3( x )		half3( x, x, x )
#define _half4( x )		half4( x, x, x, x )
#define _float2( x )	float2( x, x )
#define _float3( x )	float3( x, x, x )
#define _float4( x )	float4( x, x, x, x )

static float4 idtex2Dproj( Texture2D<float4> samp, SamplerState samplerState, half4 texCoords ) { return samp.Sample( samplerState, texCoords.xy / texCoords.w); } //TODO: old data = texCoords.xyw ); }
static float4 swizzleColor( float4 c ) { return c; }

// RGB functions
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

// Material functions
void ExtractMaterialProperties(
	in float3 specularColor,
	in float3 materialProperties,
	out float roughness,
	out float3 baseReflection,
	out float metalness
)
{
	roughness = materialProperties.x;
	metalness = materialProperties.y;

	// Approximate the base reflectivity from the specular color.
	baseReflection = specularColor * materialProperties.z;
}

// Lighting Helpers
inline float3 CalculateFresnel(in float3 baseReflectivity, in float3 viewVector, in float3 halfVector)
{
	return baseReflectivity + (1.0 - baseReflectivity) * pow(1.0 - dot3(viewVector, halfVector), 5.0);
}