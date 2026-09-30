#include "Graphics/MaterialGpuEncoder.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gglab
{
	MaterialUVTransformRows EncodeMaterialUVTransform(
		const MaterialTextureBinding& binding) noexcept
	{
		const float cosine = std::cos(binding.m_UVRotation);
		const float sine = std::sin(binding.m_UVRotation);
		return {
			.m_U = Vector4(cosine * binding.m_UVScale.m_X,
				-sine * binding.m_UVScale.m_Y, binding.m_UVOffset.m_X, 0.0f),
			.m_V = Vector4(sine * binding.m_UVScale.m_X,
				cosine * binding.m_UVScale.m_Y, binding.m_UVOffset.m_Y, 0.0f),
		};
	}

	namespace
	{
		[[nodiscard]] MaterialTextureBindingGPU EncodeTextureBinding(
			const MaterialTextureBinding& binding, ReservedTextureIDIndex fallback,
			SamplerPreset fallbackSampler, const AssetManager& assetManager,
			const RenderSamplerAccess& samplerRegistry) noexcept
		{
			const MaterialUVTransformRows transform = EncodeMaterialUVTransform(binding);
			return {
				.TextureSamplerBinding =
					{
						.TextureIndex = assetManager.ResolveSrvIndex(binding.m_TextureId, fallback),
						.SamplerIndex = samplerRegistry.ResolveSamplerIndex(
							binding.m_SamplerId, fallbackSampler),
					},
				.TexCoordIndex = binding.m_TexCoordIndex,
				.TextureEnabled = binding.m_TextureId.IsValid() ? 1u : 0u,
				.UVTransformU = transform.m_U,
				.UVTransformV = transform.m_V,
			};
		}
	}

	MaterialGPU MaterialGpuEncoder::Encode(const MaterialProperties& material,
		const AssetManager& assetManager, const RenderSamplerAccess& samplerRegistry) noexcept
	{
		MaterialGPU gpu{};
		gpu.BaseColorBinding = EncodeTextureBinding(material.m_BaseColorBinding,
			ReservedTextureIDIndex::BaseColorWhite, SamplerPreset::LinearWrap, assetManager,
			samplerRegistry);
		gpu.EmissiveBinding =
			EncodeTextureBinding(material.m_EmissiveBinding, ReservedTextureIDIndex::EmissiveWhite,
				SamplerPreset::LinearWrap, assetManager, samplerRegistry);
		gpu.MetallicRoughnessBinding = EncodeTextureBinding(material.m_MetallicRoughnessBinding,
			ReservedTextureIDIndex::DefaultMetallicRoughness, SamplerPreset::LinearWrap,
			assetManager, samplerRegistry);
		gpu.NormalBinding =
			EncodeTextureBinding(material.m_NormalBinding, ReservedTextureIDIndex::NormalFlat,
				SamplerPreset::LinearWrap, assetManager, samplerRegistry);
		gpu.OcclusionBinding = EncodeTextureBinding(material.m_OcclusionBinding,
			ReservedTextureIDIndex::OcclusionWhite, SamplerPreset::LinearWrap, assetManager,
			samplerRegistry);
		gpu.ClearcoatBinding = EncodeTextureBinding(material.m_ClearcoatBinding,
			ReservedTextureIDIndex::OcclusionWhite, SamplerPreset::LinearWrap, assetManager,
			samplerRegistry);
		gpu.ClearcoatRoughnessBinding = EncodeTextureBinding(material.m_ClearcoatRoughnessBinding,
			ReservedTextureIDIndex::OcclusionWhite, SamplerPreset::LinearWrap, assetManager,
			samplerRegistry);
		gpu.ClearcoatNormalBinding = EncodeTextureBinding(material.m_ClearcoatNormalBinding,
			ReservedTextureIDIndex::NormalFlat, SamplerPreset::LinearWrap, assetManager,
			samplerRegistry);
		gpu.AnisotropyBinding = EncodeTextureBinding(material.m_AnisotropyBinding,
			ReservedTextureIDIndex::AnisotropyDefault, SamplerPreset::LinearWrap, assetManager,
			samplerRegistry);

		gpu.BaseColorFactor = material.m_BaseColor;
		gpu.EmissiveColorFactor = material.m_EmissiveColor;
		gpu.MetallicFactor = material.m_MetallicFactor;
		gpu.RoughnessFactor = material.m_RoughnessFactor;
		gpu.NormalScale = material.m_NormalScale;
		gpu.OcclusionStrength = material.m_OcclusionStrength;
		gpu.AlphaMode = static_cast<int32_t>(material.m_AlphaMode);
		gpu.AlphaCutoff = material.m_AlphaCutoff;
		gpu.Flags = static_cast<uint32_t>(material.m_Flags);
		gpu.DebugView = static_cast<uint32_t>(material.m_DebugView);
		gpu.Ior = SanitizeMaterialIor(material.m_Ior);
		gpu.ClearcoatFactor = std::isfinite(material.m_ClearcoatFactor)
			? std::clamp(material.m_ClearcoatFactor, 0.0f, 1.0f) : 0.0f;
		gpu.ClearcoatRoughness = std::isfinite(material.m_ClearcoatRoughness)
			? std::clamp(material.m_ClearcoatRoughness, 0.0f, 1.0f) : 0.0f;
		gpu.ClearcoatNormalScale = std::isfinite(material.m_ClearcoatNormalScale)
			? material.m_ClearcoatNormalScale : 1.0f;
		gpu.AnisotropyStrength = std::isfinite(material.m_AnisotropyStrength)
			? std::clamp(material.m_AnisotropyStrength, 0.0f, 1.0f) : 0.0f;
		gpu.AnisotropyRotation = std::isfinite(material.m_AnisotropyRotation)
			? material.m_AnisotropyRotation : 0.0f;
		gpu.AnisotropyTextureEnabled = material.m_AnisotropyBinding.m_TextureId.IsValid() ? 1u : 0u;
		return gpu;
	}
}
