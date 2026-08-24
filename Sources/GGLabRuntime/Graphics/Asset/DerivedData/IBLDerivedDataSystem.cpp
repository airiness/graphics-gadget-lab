#include "Graphics/Asset/DerivedData/IBLDerivedDataSystem.h"
#include "Graphics/Asset/DerivedData/IBLStageArtifactCodec.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr std::array<std::string_view, static_cast<size_t>(IBLArtifactStage::Count)>
			ArtifactTypes = {
				"gglab.ibl.environment",
				"gglab.ibl.irradiance",
				"gglab.ibl.prefiltered_specular",
				"gglab.ibl.brdf_lut",
			};

		bool AddCommonKeyFields(DerivedDataKeyBuilder& builder, IBLArtifactStage stage,
			IBLArtifactCompatibility compatibility, std::string_view adapterScopeIdentity,
			const SourceDigest& shaderDigest) noexcept
		{
			bool succeeded = builder.AddStringUtf8(GetIBLStageArtifactType(stage));
			succeeded &= builder.AddU32LE(IBLStageArtifactSchemaVersion);
			succeeded &= builder.AddU32LE(TextureArtifactSchemaVersion);
			succeeded &= builder.AddU32LE(IBLStageProducerCompatibilityVersion);
			succeeded &= builder.AddU32LE(IBLBakeAlgorithmVersion);
			succeeded &= builder.AddU32LE(static_cast<uint32_t>(compatibility));
			if (compatibility == IBLArtifactCompatibility::AdapterScoped)
			{
				succeeded &= builder.AddStringUtf8(adapterScopeIdentity);
			}
			succeeded &= builder.AddSourceDigest(shaderDigest);
			return succeeded;
		}
	}

	std::string_view GetIBLStageArtifactType(IBLArtifactStage stage) noexcept
	{
		return stage < IBLArtifactStage::Count ? ArtifactTypes[static_cast<size_t>(stage)]
			: std::string_view{};
	}

	IBLDerivedDataSystem::IBLDerivedDataSystem(const CreateInfo& createInfo) noexcept :
		m_Compatibility(createInfo.m_Compatibility),
		m_AdapterScopeIdentity(createInfo.m_AdapterScopeIdentity),
		m_ArtifactCache(createInfo.m_ArtifactCache), m_Store(createInfo.m_CacheDirectory)
	{
	}

	IBLDerivedDataLookupResult IBLDerivedDataSystem::Lookup(
		const AssetContentFingerprint& contentFingerprint, EnvironmentTextureSourceType sourceType,
		const IBLBakeConfig& config,
		const ShaderProgramRegistryArtifactRef& shaderRegistryRef, bool ignoreCache,
		std::stop_token stopToken) noexcept
	{
		IBLDerivedDataLookupResult result{};
		const auto keys =
			BuildKeys(contentFingerprint, sourceType, config, shaderRegistryRef, stopToken);
		for (size_t index = 0; index < result.m_Stages.size(); ++index)
		{
			const IBLArtifactStage stage = static_cast<IBLArtifactStage>(index);
			auto& stageResult = result.m_Stages[index];
			stageResult.m_Key = keys[index];
			if (!stageResult.m_Key.IsValid() || stopToken.stop_requested() || ignoreCache)
			{
				continue;
			}

			{
				std::scoped_lock lock(m_ArtifactMutex);
				stageResult.m_Artifact = m_ArtifactCache.Find(stageResult.m_Key);
				if (stageResult.m_Artifact && stageResult.m_Artifact->m_Stage == stage &&
					stageResult.m_Artifact->MatchesConfig(config))
				{
					stageResult.m_Source = IBLDerivedDataSource::CpuCache;
					continue;
				}
				stageResult.m_Artifact.reset();
			}

			const std::string_view artifactType = GetIBLStageArtifactType(stage);
			const LocalDerivedDataReadOptions readOptions{
				.m_MaxContainerBytes = ComputeLocalDerivedDataContainerByteLimit(
					artifactType, IBLStageArtifactCodec::GetMaximumSerializedBytes()),
			};
			DerivedDataReadResult read = m_Store.Read(
				stageResult.m_Key, artifactType, IBLStageArtifactSchemaVersion, readOptions);
			if (stopToken.stop_requested() || read.m_Disposition != DerivedDataReadDisposition::Hit)
			{
				continue;
			}
			IBLStageArtifactDecodeResult decoded = IBLStageArtifactCodec::Deserialize(
				read.m_Payload, stage, read.m_ArtifactContentDigest);
			if (!decoded.Succeeded() || !decoded.m_Artifact.MatchesConfig(config))
			{
				m_Store.DiscardObservedCorrupt(stageResult.m_Key, artifactType,
					IBLStageArtifactSchemaVersion, read.m_ArtifactContentDigest,
					read.m_PayloadDigest, readOptions);
				stageResult.m_Error =
					decoded.m_Error.empty()
					? "IBL stage DDC artifact does not match its bake configuration."
					: std::move(decoded.m_Error);
				continue;
			}
			stageResult.m_Artifact = Admit(stageResult.m_Key,
				std::make_shared<const IBLStageArtifact>(std::move(decoded.m_Artifact)));
			stageResult.m_Source = stageResult.m_Artifact ? IBLDerivedDataSource::LocalDdc
				: IBLDerivedDataSource::Miss;
		}
		return result;
	}

	IBLStageArtifactHandle IBLDerivedDataSystem::Admit(
		const DerivedDataKey& key, IBLStageArtifactHandle artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return {};
		}
		std::scoped_lock lock(m_ArtifactMutex);
		return m_ArtifactCache.Admit(key, std::move(artifact));
	}

	bool IBLDerivedDataSystem::Store(
		const DerivedDataKey& key, const IBLStageArtifactHandle& artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return false;
		}
		const std::vector<std::byte> payload = IBLStageArtifactCodec::Serialize(*artifact);
		return !payload.empty() &&
			m_Store.Write(key, GetIBLStageArtifactType(artifact->m_Stage),
				IBLStageArtifactSchemaVersion, artifact->m_ContentDigest, payload);
	}

	IBLStageArtifactCacheStatistics IBLDerivedDataSystem::GetArtifactCacheStatistics()
		const noexcept
	{
		std::scoped_lock lock(m_ArtifactMutex);
		return m_ArtifactCache.GetStatistics();
	}

	LocalDerivedDataStoreStatistics IBLDerivedDataSystem::GetStoreStatistics() const noexcept
	{
		return m_Store.GetStatistics();
	}

	void IBLDerivedDataSystem::ClearArtifactCache() noexcept
	{
		std::scoped_lock lock(m_ArtifactMutex);
		m_ArtifactCache.Clear();
	}

	bool IBLDerivedDataSystem::ClearStore() noexcept
	{
		return m_Store.Clear();
	}

	std::array<DerivedDataKey, static_cast<size_t>(IBLArtifactStage::Count)> IBLDerivedDataSystem::
		BuildKeys(const AssetContentFingerprint& contentFingerprint,
			EnvironmentTextureSourceType sourceType, const IBLBakeConfig& config,
			const ShaderProgramRegistryArtifactRef& shaderRegistryRef,
			std::stop_token stopToken) const noexcept
	{
		std::array<DerivedDataKey, static_cast<size_t>(IBLArtifactStage::Count)> keys{};
		if (!contentFingerprint.IsValid() || !shaderRegistryRef.IsValid() ||
			stopToken.stop_requested() ||
			(m_Compatibility == IBLArtifactCompatibility::AdapterScoped &&
				m_AdapterScopeIdentity.empty()))
		{
			return keys;
		}

		// The registry is a content-addressed snapshot of every executable shader
		// artifact used by this runtime. It is a conservative but source-free IBL
		// producer identity: any activated shader-set change invalidates stale bakes.
		const SourceDigest shaderArtifactSetDigest{
			.m_Value = shaderRegistryRef.m_RegistryId.m_DurableDigest.m_Value,
		};

		DerivedDataKeyBuilder environment;
		bool succeeded = AddCommonKeyFields(environment, IBLArtifactStage::Environment,
			m_Compatibility, m_AdapterScopeIdentity, shaderArtifactSetDigest);
		succeeded &= environment.AddU64LE(contentFingerprint.m_SourceContentHash);
		succeeded &= environment.AddU64LE(contentFingerprint.m_ImportSettingsHash);
		succeeded &= environment.AddU32LE(contentFingerprint.m_DecoderVersion);
		succeeded &= environment.AddU32LE(static_cast<uint32_t>(sourceType));
		succeeded &= environment.AddU32LE(config.m_EnvironmentCubemapSize);
		succeeded &= environment.AddU32LE(static_cast<uint32_t>(config.m_EnvironmentCubemapFormat));
		keys[static_cast<size_t>(IBLArtifactStage::Environment)] =
			succeeded ? environment.Finish() : DerivedDataKey{};

		DerivedDataKeyBuilder irradiance;
		succeeded = AddCommonKeyFields(irradiance, IBLArtifactStage::Irradiance, m_Compatibility,
			m_AdapterScopeIdentity, shaderArtifactSetDigest);
		succeeded &=
			irradiance.AddDerivedDataKey(keys[static_cast<size_t>(IBLArtifactStage::Environment)]);
		succeeded &= irradiance.AddU32LE(config.m_IrradianceCubemapSize);
		succeeded &= irradiance.AddU32LE(static_cast<uint32_t>(config.m_IrradianceCubemapFormat));
		succeeded &= irradiance.AddU32LE(config.m_IrradianceSampleCount);
		keys[static_cast<size_t>(IBLArtifactStage::Irradiance)] =
			succeeded ? irradiance.Finish() : DerivedDataKey{};

		DerivedDataKeyBuilder specular;
		succeeded = AddCommonKeyFields(specular, IBLArtifactStage::PrefilteredSpecular,
			m_Compatibility, m_AdapterScopeIdentity, shaderArtifactSetDigest);
		succeeded &=
			specular.AddDerivedDataKey(keys[static_cast<size_t>(IBLArtifactStage::Environment)]);
		succeeded &= specular.AddU32LE(config.m_PrefilteredSpecularCubemapSize);
		succeeded &= specular.AddU32LE(config.m_PrefilteredSpecularMipLevels);
		succeeded &=
			specular.AddU32LE(static_cast<uint32_t>(config.m_PrefilteredSpecularCubemapFormat));
		succeeded &= specular.AddU32LE(config.m_PrefilteredSpecularSampleCount);
		succeeded &= specular.AddU32LE(
			std::bit_cast<uint32_t>(config.m_PrefilteredSpecularMaxSampleLuminance));
		keys[static_cast<size_t>(IBLArtifactStage::PrefilteredSpecular)] =
			succeeded ? specular.Finish() : DerivedDataKey{};

		DerivedDataKeyBuilder brdf;
		succeeded = AddCommonKeyFields(brdf, IBLArtifactStage::BrdfLut, m_Compatibility,
			m_AdapterScopeIdentity, shaderArtifactSetDigest);
		succeeded &= brdf.AddU32LE(config.m_BrdfLutSize);
		succeeded &= brdf.AddU32LE(static_cast<uint32_t>(config.m_BrdfLutFormat));
		keys[static_cast<size_t>(IBLArtifactStage::BrdfLut)] =
			succeeded ? brdf.Finish() : DerivedDataKey{};
		return keys;
	}

}
