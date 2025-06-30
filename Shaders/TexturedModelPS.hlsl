struct PixelShaderInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
    float3 Color : COLOR;
};

Texture2D tex01 : register(t0);
SamplerState sampler01 : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    return tex01.Sample(sampler01, IN.TexCoord);
    //float3 color = abs(IN.Color);
    //return float4(color, 1.0);
}