#include "Core/Precompiled.h"
#include "Graphics/MaterialGpuEncoder.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] MaterialTextureBindingGPU EncodeTextureBinding(
			const MaterialTextureBinding& binding, ReservedTextureIDIndex fallback,
			SamplerPreset fallbackSampler, const AssetManager& assetManager,
			const SamplerRegistry& samplerRegistry) noexcept
		{
			return {
				.TextureSamplerBinding =
					{
						.TextureIndex = assetManager.ResolveSrvIndex(binding.m_TextureId, fallback),
						.SamplerIndex = samplerRegistry.ResolveSamplerIndex(
							binding.m_SamplerId, fallbackSampler),
					},
				.TexCoordIndex = binding.m_TexCoordIndex,
				.Padding = 0,
			};
		}
	}

	MaterialGPU MaterialGpuEncoder::Encode(const MaterialProperties& material,
		const AssetManager& assetManager, const SamplerRegistry& samplerRegistry) noexcept
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
		return gpu;
	}
}
