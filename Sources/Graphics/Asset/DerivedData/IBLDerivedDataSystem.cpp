#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/IBLDerivedDataSystem.h"
#include "Graphics/Asset/DerivedData/IBLStageArtifactCodec.h"
#include "Graphics/Shader/ShaderPaths.h"

#include <bit>
#include <fstream>
#include <set>

namespace gglab
{
	namespace
	{
		constexpr std::array<std::string_view,
			static_cast<size_t>(IBLArtifactStage::Count)> ArtifactTypes = {
			"gglab.ibl.environment",
			"gglab.ibl.irradiance",
			"gglab.ibl.prefiltered_specular",
			"gglab.ibl.brdf_lut",
		};

		constexpr std::array<std::array<std::string_view, 2>,
			static_cast<size_t>(IBLArtifactStage::Count)> BakeShaderPaths = {{
			{{ "Passes/PassIBLEnvironment.hlsl", "Passes/PassIBLEnvironmentMip.hlsl" }},
			{{ "Passes/PassIBLIrradiance.hlsl", {} }},
			{{ "Passes/PassIBLPrefilteredSpecular.hlsl", {} }},
			{{ "Passes/PassIBLBrdfLUT.hlsl", {} }},
		}};

		bool AddShaderDependency(
			Sha256Builder& builder,
			const std::filesystem::path& shaderRoot,
			const std::filesystem::path& path,
			std::set<std::filesystem::path>& visited,
			std::stop_token stopToken) noexcept
		{
			if (stopToken.stop_requested())
			{
				return false;
			}
			std::error_code errorCode;
			const std::filesystem::path normalized =
				std::filesystem::weakly_canonical(path, errorCode);
			const std::filesystem::path key = errorCode ? path.lexically_normal() : normalized;
			if (!visited.insert(key).second)
			{
				return true;
			}

			std::error_code relativeError;
			const std::filesystem::path canonicalRoot =
				std::filesystem::weakly_canonical(shaderRoot, relativeError);
			const std::filesystem::path relativePath = relativeError ? key :
				std::filesystem::relative(key, canonicalRoot, relativeError);
			const std::string pathText = (relativeError ? key : relativePath).generic_string();
			bool succeeded = builder.AddU64(static_cast<uint64_t>(pathText.size()));
			succeeded &= builder.AddStringUtf8(pathText);

			std::ifstream stream(key, std::ios::binary);
			if (!stream)
			{
				succeeded &= builder.AddStringUtf8("missing-shader-dependency");
				return succeeded;
			}
			std::string source(
				(std::istreambuf_iterator<char>(stream)),
				std::istreambuf_iterator<char>());
			succeeded &= builder.AddU64(static_cast<uint64_t>(source.size()));
			succeeded &= builder.AddBytes(std::as_bytes(std::span(source)));

			size_t cursor = 0;
			while ((cursor = source.find("#include", cursor)) != std::string::npos)
			{
				const size_t delimiter = source.find_first_of("\"<", cursor + 8);
				if (delimiter == std::string::npos) break;
				const char closing = source[delimiter] == '<' ? '>' : '\"';
				const size_t end = source.find(closing, delimiter + 1);
				if (end == std::string::npos) break;
				const std::filesystem::path include = source.substr(
					delimiter + 1,
					end - delimiter - 1);
				succeeded &= AddShaderDependency(
					builder,
					shaderRoot,
					source[delimiter] == '<' ? shaderRoot / include : key.parent_path() / include,
					visited,
					stopToken);
				cursor = end + 1;
			}
			return succeeded;
		}

		bool AddCommonKeyFields(
			DerivedDataKeyBuilder& builder,
			IBLArtifactStage stage,
			IBLArtifactCompatibility compatibility,
			std::string_view adapterScopeIdentity,
			const SourceDigest& shaderDigest) noexcept
		{
			bool succeeded = builder.AddStringUtf8(GetIBLStageArtifactType(stage));
			succeeded &= builder.AddU32(IBLStageArtifactSchemaVersion);
			succeeded &= builder.AddU32(TextureArtifactSchemaVersion);
			succeeded &= builder.AddU32(IBLStageProducerCompatibilityVersion);
			succeeded &= builder.AddU32(IBLBakeAlgorithmVersion);
			succeeded &= builder.AddU32(static_cast<uint32_t>(compatibility));
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
		return stage < IBLArtifactStage::Count ?
			ArtifactTypes[static_cast<size_t>(stage)] : std::string_view{};
	}

	IBLDerivedDataSystem::IBLDerivedDataSystem(const CreateInfo& createInfo) noexcept :
		m_Compatibility(createInfo.m_Compatibility),
		m_AdapterScopeIdentity(createInfo.m_AdapterScopeIdentity),
		m_ArtifactCache(createInfo.m_ArtifactCache),
		m_Store(createInfo.m_CacheDirectory)
	{}

	IBLDerivedDataLookupResult IBLDerivedDataSystem::Lookup(
		const AssetContentFingerprint& contentFingerprint,
		EnvironmentTextureSourceType sourceType,
		const IBLBakeConfig& config,
		bool ignoreCache,
		std::stop_token stopToken) noexcept
	{
		IBLDerivedDataLookupResult result{};
		const auto keys = BuildKeys(contentFingerprint, sourceType, config, stopToken);
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
				if (stageResult.m_Artifact &&
					stageResult.m_Artifact->m_Stage == stage &&
					stageResult.m_Artifact->MatchesConfig(config))
				{
					stageResult.m_Source = IBLDerivedDataSource::CpuCache;
					continue;
				}
				stageResult.m_Artifact.reset();
			}

			const std::string_view artifactType = GetIBLStageArtifactType(stage);
			DerivedDataReadResult read = m_Store.Read(
				stageResult.m_Key,
				artifactType,
				IBLStageArtifactSchemaVersion,
				{
					.m_MaxContainerBytes = ComputeLocalDerivedDataContainerByteLimit(
						artifactType,
						IBLStageArtifactCodec::GetMaximumSerializedBytes()),
				});
			if (stopToken.stop_requested() ||
				read.m_Disposition != DerivedDataReadDisposition::Hit)
			{
				continue;
			}
			IBLStageArtifactDecodeResult decoded = IBLStageArtifactCodec::Deserialize(
				read.m_Payload,
				stage,
				read.m_ArtifactContentDigest);
			if (!decoded.Succeeded() || !decoded.m_Artifact.MatchesConfig(config))
			{
				m_Store.DiscardCorrupt(stageResult.m_Key);
				stageResult.m_Error = decoded.m_Error.empty() ?
					"IBL stage DDC artifact does not match its bake configuration." :
					std::move(decoded.m_Error);
				continue;
			}
			stageResult.m_Artifact = Admit(
				stageResult.m_Key,
				std::make_shared<const IBLStageArtifact>(std::move(decoded.m_Artifact)));
			stageResult.m_Source = stageResult.m_Artifact ?
				IBLDerivedDataSource::LocalDdc : IBLDerivedDataSource::Miss;
		}
		return result;
	}

	IBLStageArtifactHandle IBLDerivedDataSystem::Admit(
		const DerivedDataKey& key,
		IBLStageArtifactHandle artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return {};
		}
		std::scoped_lock lock(m_ArtifactMutex);
		return m_ArtifactCache.Admit(key, std::move(artifact));
	}

	bool IBLDerivedDataSystem::Store(
		const DerivedDataKey& key,
		const IBLStageArtifactHandle& artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return false;
		}
		const std::vector<std::byte> payload = IBLStageArtifactCodec::Serialize(*artifact);
		return !payload.empty() && m_Store.Write(
			key,
			GetIBLStageArtifactType(artifact->m_Stage),
			IBLStageArtifactSchemaVersion,
			artifact->m_ContentDigest,
			payload);
	}

	IBLStageArtifactCacheStatistics
	IBLDerivedDataSystem::GetArtifactCacheStatistics() const noexcept
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

	std::array<DerivedDataKey, static_cast<size_t>(IBLArtifactStage::Count)>
	IBLDerivedDataSystem::BuildKeys(
		const AssetContentFingerprint& contentFingerprint,
		EnvironmentTextureSourceType sourceType,
		const IBLBakeConfig& config,
		std::stop_token stopToken) const noexcept
	{
		std::array<DerivedDataKey, static_cast<size_t>(IBLArtifactStage::Count)> keys{};
		if (!contentFingerprint.IsValid() || stopToken.stop_requested() ||
			(m_Compatibility == IBLArtifactCompatibility::AdapterScoped &&
				m_AdapterScopeIdentity.empty()))
		{
			return keys;
		}

		std::array<SourceDigest, static_cast<size_t>(IBLArtifactStage::Count)> shaderDigests{};
		for (size_t index = 0; index < shaderDigests.size(); ++index)
		{
			shaderDigests[index] = ComputeShaderDependencyDigest(
				static_cast<IBLArtifactStage>(index),
				stopToken);
			if (!shaderDigests[index].IsValid())
			{
				return {};
			}
		}

		DerivedDataKeyBuilder environment;
		bool succeeded = AddCommonKeyFields(
			environment,
			IBLArtifactStage::Environment,
			m_Compatibility,
			m_AdapterScopeIdentity,
			shaderDigests[static_cast<size_t>(IBLArtifactStage::Environment)]);
		succeeded &= environment.AddU64(contentFingerprint.m_SourceContentHash);
		succeeded &= environment.AddU64(contentFingerprint.m_ImportSettingsHash);
		succeeded &= environment.AddU32(contentFingerprint.m_DecoderVersion);
		succeeded &= environment.AddU32(static_cast<uint32_t>(sourceType));
		succeeded &= environment.AddU32(config.m_EnvironmentCubemapSize);
		succeeded &= environment.AddU32(static_cast<uint32_t>(config.m_EnvironmentCubemapFormat));
		keys[static_cast<size_t>(IBLArtifactStage::Environment)] =
			succeeded ? environment.Finish() : DerivedDataKey{};

		DerivedDataKeyBuilder irradiance;
		succeeded = AddCommonKeyFields(
			irradiance,
			IBLArtifactStage::Irradiance,
			m_Compatibility,
			m_AdapterScopeIdentity,
			shaderDigests[static_cast<size_t>(IBLArtifactStage::Irradiance)]);
		succeeded &= irradiance.AddDerivedDataKey(
			keys[static_cast<size_t>(IBLArtifactStage::Environment)]);
		succeeded &= irradiance.AddU32(config.m_IrradianceCubemapSize);
		succeeded &= irradiance.AddU32(static_cast<uint32_t>(config.m_IrradianceCubemapFormat));
		succeeded &= irradiance.AddU32(config.m_IrradianceSampleCount);
		keys[static_cast<size_t>(IBLArtifactStage::Irradiance)] =
			succeeded ? irradiance.Finish() : DerivedDataKey{};

		DerivedDataKeyBuilder specular;
		succeeded = AddCommonKeyFields(
			specular,
			IBLArtifactStage::PrefilteredSpecular,
			m_Compatibility,
			m_AdapterScopeIdentity,
			shaderDigests[static_cast<size_t>(IBLArtifactStage::PrefilteredSpecular)]);
		succeeded &= specular.AddDerivedDataKey(
			keys[static_cast<size_t>(IBLArtifactStage::Environment)]);
		succeeded &= specular.AddU32(config.m_PrefilteredSpecularCubemapSize);
		succeeded &= specular.AddU32(config.m_PrefilteredSpecularMipLevels);
		succeeded &= specular.AddU32(static_cast<uint32_t>(
			config.m_PrefilteredSpecularCubemapFormat));
		succeeded &= specular.AddU32(config.m_PrefilteredSpecularSampleCount);
		succeeded &= specular.AddU32(std::bit_cast<uint32_t>(
			config.m_PrefilteredSpecularMaxSampleLuminance));
		keys[static_cast<size_t>(IBLArtifactStage::PrefilteredSpecular)] =
			succeeded ? specular.Finish() : DerivedDataKey{};

		DerivedDataKeyBuilder brdf;
		succeeded = AddCommonKeyFields(
			brdf,
			IBLArtifactStage::BrdfLut,
			m_Compatibility,
			m_AdapterScopeIdentity,
			shaderDigests[static_cast<size_t>(IBLArtifactStage::BrdfLut)]);
		succeeded &= brdf.AddU32(config.m_BrdfLutSize);
		succeeded &= brdf.AddU32(static_cast<uint32_t>(config.m_BrdfLutFormat));
		keys[static_cast<size_t>(IBLArtifactStage::BrdfLut)] =
			succeeded ? brdf.Finish() : DerivedDataKey{};
		return keys;
	}

	SourceDigest IBLDerivedDataSystem::ComputeShaderDependencyDigest(
		IBLArtifactStage stage,
		std::stop_token stopToken) const noexcept
	{
		if (stage >= IBLArtifactStage::Count)
		{
			return {};
		}
		Sha256Builder builder;
		bool succeeded = builder.IsValid();
		const std::filesystem::path shaderRoot = GetShaderSourceRoot();
		std::set<std::filesystem::path> visited;
		for (std::string_view shaderPath : BakeShaderPaths[static_cast<size_t>(stage)])
		{
			if (shaderPath.empty())
			{
				continue;
			}
			succeeded &= AddShaderDependency(
				builder,
				shaderRoot,
				shaderRoot / shaderPath,
				visited,
				stopToken);
		}
		if (!succeeded || stopToken.stop_requested())
		{
			return {};
		}
		SourceDigest digest{};
		digest.m_Value = builder.Finish().m_Value;
		return digest;
	}
}
