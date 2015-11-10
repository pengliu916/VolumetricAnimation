struct VSOutput {
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};
float4x4 mtx;

VSOutput vsmain(
	float4 pos : POSITION,
	float4 color : COLOR
	)
{
	VSOutput vsout = ( VSOutput ) 0;
	vsout.Position = mul( mtx, pos );
	vsout.Color = color;
	return vsout;
}


float4 psmain( VSOutput vsout ) : SV_TARGET
{
	return vsout.Color;
}