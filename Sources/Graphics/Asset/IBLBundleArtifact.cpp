#include "Core/Precompiled.h"
#include "Graphics/Asset/IBLBundleArtifact.h"
#include "Core/Hash/Sha256.h"
#include "Graphics/Asset/TextureArtifact.h"
#include "Graphics/Utility/TextureUtils.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool MatchesTexture(
			const TextureAssetData& texture,
			uint32_t size,
			uint16_t arraySize,
			uint16_t mipLevels,
			RHIFormat format) noexcept
		{
			return texture.IsValid() &&
				texture.m_Extent.m_Width == size &&
				texture.m_Extent.m_Height == size &&
				texture.m_ArraySize == arraySize &&
				texture.m_MipLevels == mipLevels &&
				texture.m_ViewFormat == format;
		}

		[[nodiscard]] uint64_t TextureAllocatedBytes(
			const TextureAssetData& texture) noexcept
		{
			return sizeof(TextureAssetData) +
				static_cast<uint64_t>(texture.m_Pixels.capacity()) * sizeof(std::byte) +
				static_cast<uint64_t>(texture.m_Subresources.capacity()) *
					sizeof(TextureAssetSubresource);
		}
	}

	bool IBLBundleArtifact::IsValid() const noexcept
	{
		return m_Environment.IsValid() && m_Irradiance.IsValid() &&
			m_PrefilteredSpecular.IsValid() && m_BrdfLut.IsValid() &&
			m_ContentDigest.IsValid();
	}

	bool IBLBundleArtifact::MatchesConfig(const IBLBakeConfig& config) const noexcept
	{
		const uint32_t environmentSize = std::max(config.m_EnvironmentCubemapSize, 1u);
		const uint32_t specularSize = std::max(
			config.m_PrefilteredSpecularCubemapSize,
			1u);
		const uint16_t specularMips = static_cast<uint16_t>(std::clamp(
			config.m_PrefilteredSpecularMipLevels,
			1u,
			CalculateMipLevelCount(specularSize)));
		return MatchesTexture(
			m_Environment,
			environmentSize,
			static_cast<uint16_t>(CubemapFaceCount),
			static_cast<uint16_t>(CalculateMipLevelCount(environmentSize)),
			config.m_EnvironmentCubemapFormat) &&
			MatchesTexture(
				m_Irradiance,
				std::max(config.m_IrradianceCubemapSize, 1u),
				static_cast<uint16_t>(CubemapFaceCount),
				1,
				config.m_IrradianceCubemapFormat) &&
			MatchesTexture(
				m_PrefilteredSpecular,
				specularSize,
				static_cast<uint16_t>(CubemapFaceCount),
				specularMips,
				config.m_PrefilteredSpecularCubemapFormat) &&
			MatchesTexture(
				m_BrdfLut,
				std::max(config.m_BrdfLutSize, 1u),
				1,
				1,
				config.m_BrdfLutFormat);
	}

	uint64_t IBLBundleArtifact::GetAllocatedBytes() const noexcept
	{
		return sizeof(IBLBundleArtifact) +
			TextureAllocatedBytes(m_Environment) +
			TextureAllocatedBytes(m_Irradiance) +
			TextureAllocatedBytes(m_PrefilteredSpecular) +
			TextureAllocatedBytes(m_BrdfLut) -
			4ull * sizeof(TextureAssetData);
	}

	ArtifactContentDigest ComputeIBLBundleArtifactContentDigest(
		const IBLBundleArtifact& artifact) noexcept
	{
		const std::array digests{
			ComputeTextureArtifactContentDigest(artifact.m_Environment),
			ComputeTextureArtifactContentDigest(artifact.m_Irradiance),
			ComputeTextureArtifactContentDigest(artifact.m_PrefilteredSpecular),
			ComputeTextureArtifactContentDigest(artifact.m_BrdfLut),
		};
		if (std::ranges::any_of(digests, std::not_fn(&ArtifactContentDigest::IsValid)))
		{
			return {};
		}

		Sha256Builder builder;
		bool succeeded = builder.IsValid();
		succeeded &= builder.AddStringUtf8("gglab.ibl.bundle.content");
		succeeded &= builder.AddU32(IBLBundleArtifactSchemaVersion);
		for (const ArtifactContentDigest& digest : digests)
		{
			succeeded &= builder.AddBytes(digest.m_Value);
		}
		if (!succeeded)
		{
			return {};
		}
		ArtifactContentDigest digest{};
		digest.m_Value = builder.Finish().m_Value;
		return digest;
	}

	IBLBundleArtifactHandle CreateIBLBundleArtifact(
		IBLBundleArtifact&& artifact) noexcept
	{
		artifact.m_ContentDigest = ComputeIBLBundleArtifactContentDigest(artifact);
		if (!artifact.IsValid())
		{
			return {};
		}
		return std::make_shared<const IBLBundleArtifact>(std::move(artifact));
	}
}
