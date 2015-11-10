#include"D3DX_DXGIFormatConvert.inl"// this file provide utility funcs for format conversion
SamplerState samRaycast : register( s0 );
StructuredBuffer<uint> g_bufVolumeSRV : register( t0 );
RWStructuredBuffer<uint> g_bufVolumeUAV : register( u0 );

cbuffer cbChangesEveryFrame : register( b0 )
{
	float4x4 worldViewProj;
	float4 viewPos;

	// comment out the following two line and uncomment the next static uint4 colVal[6] block
	// will increase the frametime from 5.5ms to 21ms!!
	uint4 colVal[6];
	uint4 bgCol;
};

// Comment out the uint4 colVal1[6]; uint4 bgCol1; two lines and uncomment this block will
// increase the frametime from 5.5ms to 21ms!!
//static uint4 colVal[6] = {
//	uint4( 1, 0, 0, 0 ),
//	uint4( 0, 1, 0, 1 ),
//	uint4( 0, 0, 1, 2 ),
//	uint4( 1, 1, 0, 3 ),
//	uint4( 1, 0, 1, 4 ),
//	uint4( 0, 1, 1, 5 )
//};
//static uint4 bgCol = uint4( 64, 64, 64, 64 );

// TSDF related variable
static const float3 voxelResolution = float3( 256, 256, 256 );// float3( VOLUME_SIZE, VOLUME_SIZE, VOLUME_SIZE );
static const float3 boxMin = float3( -1.0, -1.0, -1.0 )*voxelResolution / 2.0f;
static const float3 boxMax = float3( 1.0, 1.0, 1.0 )*voxelResolution / 2.0f;
static const float3 reversedWidthHeightDepth = 1.0f / ( voxelResolution );

static const float density = 0.01;

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct VSOutput {
	float4 ProjPos : SV_POSITION;
	float4 Pos : COLOR;
};

struct Ray
{
	float4 o;
	float4 d;
};

//--------------------------------------------------------------------------------------
// Utility Functions
//--------------------------------------------------------------------------------------
bool IntersectBox( Ray r, float3 boxmin, float3 boxmax, out float tnear, out float tfar )
{
	// compute intersection of ray with all six bbox planes
	float3 invR = 1.0 / r.d.xyz;
	float3 tbot = invR * ( boxmin.xyz - r.o.xyz );
	float3 ttop = invR * ( boxmax.xyz - r.o.xyz );

	// re-order intersections to find smallest and largest on each axis
	float3 tmin = min( ttop, tbot );
	float3 tmax = max( ttop, tbot );

	// find the largest tmin and the smallest tmax
	float2 t0 = max( tmin.xx, tmin.yz );
	tnear = max( t0.x, t0.y );
	t0 = min( tmax.xx, tmax.yz );
	tfar = min( t0.x, t0.y );

	return tnear <= tfar;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VSOutput vsmain( float4 pos : POSITION )
{
	VSOutput vsout = ( VSOutput ) 0;
	vsout.ProjPos = mul( worldViewProj, pos );
	vsout.Pos = pos;
	return vsout;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 psmain( VSOutput input ) : SV_TARGET
{
	float4 output = float4 ( 0, 0, 0, 0 );

	Ray eyeray;
	//world space
	eyeray.o = viewPos;
	eyeray.d = input.Pos - eyeray.o;
	eyeray.d = normalize( eyeray.d );
	eyeray.d.x = ( eyeray.d.x == 0.f ) ? 1e-15 : eyeray.d.x;
	eyeray.d.y = ( eyeray.d.y == 0.f ) ? 1e-15 : eyeray.d.y;
	eyeray.d.z = ( eyeray.d.z == 0.f ) ? 1e-15 : eyeray.d.z;

	// calculate ray intersection with bounding box
	float tnear, tfar;
	bool hit = IntersectBox( eyeray, boxMin, boxMax , tnear, tfar );
	if ( !hit ) return output;

	// calculate intersection points
	float3 Pnear = eyeray.o.xyz + eyeray.d.xyz * tnear;
	float3 Pfar = eyeray.o.xyz + eyeray.d.xyz * tfar;

	float3 P = Pnear;
	float t = tnear;
	float tSmallStep = 5;
	float3 P_pre = Pnear;
	float3 PsmallStep = eyeray.d.xyz * tSmallStep;

	float3 currentPixPos;

	while ( t <= tfar ) {
		int3 idx = P + voxelResolution * 0.5;
		float4 value = D3DX_R8G8B8A8_UINT_to_UINT4( g_bufVolumeSRV[idx.x + idx.y*voxelResolution.x + idx.z*voxelResolution.y * voxelResolution.x] ) / 256.f;

		output += value * density;

		P += PsmallStep;
		t += tSmallStep;
	}
	return output;
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------
[numthreads( 8, 8, 8 )]
void csmain( uint3 DTid: SV_DispatchThreadID, uint Tid : SV_GroupIndex )
{
	uint4 col = D3DX_R8G8B8A8_UINT_to_UINT4( g_bufVolumeUAV[DTid.x + DTid.y*voxelResolution.x + DTid.z*voxelResolution.x*voxelResolution.y] );
	col.xyz -= colVal[col.w].xyz;
	if ( !any( col.xyz - bgCol.xyz ) )
	{
		col.w = ( col.w + 1 ) % 6;
		col.xyz = 255 * colVal[col.w].xyz + bgCol.xyz; // Let it overflow, it doesn't matter
	}
	g_bufVolumeUAV[DTid.x + DTid.y*voxelResolution.x + DTid.z*voxelResolution.x*voxelResolution.y] = D3DX_UINT4_to_R8G8B8A8_UINT( col );
}

