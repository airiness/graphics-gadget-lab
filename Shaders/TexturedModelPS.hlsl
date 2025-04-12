struct PixelShaderInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

Texture2D tex01 : register(t0);
SamplerState sampler01 : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    return tex01.Sample(sampler01, IN.TexCoord);
}