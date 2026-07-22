#include "Core/Precompiled.h"
#include "Graphics/Asset/IBLStageArtifact.h"
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
	}

	bool IBLStageArtifact::IsValid() const noexcept
	{
		return m_Stage < IBLArtifactStage::Count &&
			m_Texture.IsValid() && m_ContentDigest.IsValid();
	}

	bool IBLStageArtifact::MatchesConfig(const IBLBakeConfig& config) const noexcept
	{
		switch (m_Stage)
		{
		case IBLArtifactStage::Environment:
		{
			const uint32_t size = std::max(config.m_EnvironmentCubemapSize, 1u);
			return MatchesTexture(
				m_Texture,
				size,
				static_cast<uint16_t>(CubemapFaceCount),
				static_cast<uint16_t>(CalculateMipLevelCount(size)),
				config.m_EnvironmentCubemapFormat);
		}
		case IBLArtifactStage::Irradiance:
			return MatchesTexture(
				m_Texture,
				std::max(config.m_IrradianceCubemapSize, 1u),
				static_cast<uint16_t>(CubemapFaceCount),
				1,
				config.m_IrradianceCubemapFormat);
		case IBLArtifactStage::PrefilteredSpecular:
		{
			const uint32_t size = std::max(config.m_PrefilteredSpecularCubemapSize, 1u);
			return MatchesTexture(
				m_Texture,
				size,
				static_cast<uint16_t>(CubemapFaceCount),
				static_cast<uint16_t>(std::clamp(
					config.m_PrefilteredSpecularMipLevels,
					1u,
					CalculateMipLevelCount(size))),
				config.m_PrefilteredSpecularCubemapFormat);
		}
		case IBLArtifactStage::BrdfLut:
			return MatchesTexture(
				m_Texture,
				std::max(config.m_BrdfLutSize, 1u),
				1,
				1,
				config.m_BrdfLutFormat);
		case IBLArtifactStage::Count:
			break;
		}
		return false;
	}

	uint64_t IBLStageArtifact::GetAllocatedBytes() const noexcept
	{
		return sizeof(IBLStageArtifact) +
			static_cast<uint64_t>(m_Texture.m_Pixels.capacity()) * sizeof(std::byte) +
			static_cast<uint64_t>(m_Texture.m_Subresources.capacity()) *
				sizeof(TextureAssetSubresource);
	}

	const IBLStageArtifactHandle& IBLStageArtifactSet::Get(
		IBLArtifactStage stage) const noexcept
	{
		GGLAB_ASSERT(stage < IBLArtifactStage::Count);
		return m_Artifacts[static_cast<size_t>(stage)];
	}

	void IBLStageArtifactSet::Set(
		IBLArtifactStage stage,
		IBLStageArtifactHandle artifact) noexcept
	{
		GGLAB_ASSERT(stage < IBLArtifactStage::Count);
		GGLAB_ASSERT(!artifact || artifact->m_Stage == stage);
		m_Artifacts[static_cast<size_t>(stage)] = std::move(artifact);
	}

	bool IBLStageArtifactSet::Has(IBLArtifactStage stage) const noexcept
	{
		return stage < IBLArtifactStage::Count &&
			static_cast<bool>(m_Artifacts[static_cast<size_t>(stage)]);
	}

	bool IBLStageArtifactSet::IsComplete() const noexcept
	{
		return std::ranges::all_of(
			m_Artifacts,
			[](const IBLStageArtifactHandle& artifact) noexcept
			{
				return artifact && artifact->IsValid();
			});
	}

	ArtifactContentDigest ComputeIBLStageArtifactContentDigest(
		const IBLStageArtifact& artifact) noexcept
	{
		const ArtifactContentDigest textureDigest =
			ComputeTextureArtifactContentDigest(artifact.m_Texture);
		if (artifact.m_Stage >= IBLArtifactStage::Count || !textureDigest.IsValid())
		{
			return {};
		}
		Sha256Builder builder;
		bool succeeded = builder.IsValid();
		succeeded &= builder.AddStringUtf8("gglab.ibl.stage.content");
		succeeded &= builder.AddU32(IBLStageArtifactSchemaVersion);
		succeeded &= builder.AddU32(static_cast<uint32_t>(artifact.m_Stage));
		succeeded &= builder.AddBytes(textureDigest.m_Value);
		if (!succeeded)
		{
			return {};
		}
		ArtifactContentDigest digest{};
		digest.m_Value = builder.Finish().m_Value;
		return digest;
	}

	IBLStageArtifactHandle CreateIBLStageArtifact(
		IBLArtifactStage stage,
		TextureAssetData&& texture) noexcept
	{
		if (stage == IBLArtifactStage::Environment ||
			stage == IBLArtifactStage::Irradiance ||
			stage == IBLArtifactStage::PrefilteredSpecular)
		{
			texture.m_SrvDimension = texture.m_ArraySize == CubemapFaceCount ?
				RHITextureViewDimension::TextureCube :
				RHITextureViewDimension::TextureCubeArray;
		}
		else if (stage == IBLArtifactStage::BrdfLut)
		{
			texture.m_SrvDimension = RHITextureViewDimension::Texture2D;
		}

		IBLStageArtifact artifact{
			.m_Stage = stage,
			.m_Texture = std::move(texture),
		};
		artifact.m_ContentDigest = ComputeIBLStageArtifactContentDigest(artifact);
		return artifact.IsValid() ?
			std::make_shared<const IBLStageArtifact>(std::move(artifact)) : nullptr;
	}
}
