#pragma once
#include <Common/MaterialSampling.hlsli>

static const int MaterialAlphaModeOpaque = 0;
static const int MaterialAlphaModeMask = 1;
static const int MaterialAlphaModeBlend = 2;

float2 SelectUV(MaterialTextureBindingData bindingData, float2 uv0, float2 uv1)
{
	return (bindingData.TexCoordIndex == 1u) ? uv1 : uv0;
}

float SampleMaterialBaseAlpha(MaterialData matData, float2 uv0, float2 uv1)
{
	const float2 baseColorUV = SelectUV(matData.BaseColorBinding, uv0, uv1);
	const float sampledAlpha = SampleTextureBinding(
		matData.BaseColorBinding.TextureSamplerBinding,
		baseColorUV).a;

	return sampledAlpha * matData.BaseColorFactor.a;
}

float ResolveMaterialAlpha(MaterialData matData, float sampledAlpha)
{
	if (matData.AlphaMode == MaterialAlphaModeOpaque)
	{
		return 1.0;
	}

	if (matData.AlphaMode == MaterialAlphaModeMask)
	{
		clip(sampledAlpha - matData.AlphaCutoff);
		return 1.0;
	}

	return sampledAlpha;
}

void ApplyMaterialAlphaClip(MaterialData matData, float2 uv0, float2 uv1)
{
	if (matData.AlphaMode != MaterialAlphaModeMask)
	{
		return;
	}

	clip(SampleMaterialBaseAlpha(matData, uv0, uv1) - matData.AlphaCutoff);
}
