cbuffer ConstatBuffer : register(b0)
{
	float4x4 ModelMat;
	float4x4 ViewMat;
	float4x4 ProjectionMat;
};

struct VertexPosColor
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 TexCoord : TEXCOORD;
};

struct VertexShaderOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
	float3 Color : COLOR;
};

VertexShaderOutput main(VertexPosColor IN)
{
	VertexShaderOutput OUT;

	float4 pos = float4(IN.Position, 1.0f);
	pos = mul(pos, ModelMat);
	pos = mul(pos, ViewMat);
	pos = mul(pos, ProjectionMat);
	
	OUT.Position = pos;
	OUT.TexCoord = IN.TexCoord;
	OUT.Color = IN.Normal; // Assuming Normal is used as color for demonstration

	return OUT;
}