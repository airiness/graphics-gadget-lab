#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/IBLDerivedDataSystem.h"
#include "Graphics/Asset/DerivedData/IBLBundleArtifactCodec.h"
#include "Graphics/Shader/ShaderPaths.h"

#include <bit>
#include <fstream>
#include <set>

namespace gglab
{
	namespace
	{
		constexpr std::array<std::string_view, 5> BakeShaderPaths = {
			"Passes/PassIBLEnvironment.hlsl",
			"Passes/PassIBLEnvironmentMip.hlsl",
			"Passes/PassIBLIrradiance.hlsl",
			"Passes/PassIBLPrefilteredSpecular.hlsl",
			"Passes/PassIBLBrdfLUT.hlsl",
		};

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
	}

	IBLDerivedDataSystem::IBLDerivedDataSystem(const CreateInfo& createInfo) noexcept :
		m_Compatibility(createInfo.m_Compatibility),
		m_AdapterScopeIdentity(createInfo.m_AdapterScopeIdentity),
		m_ArtifactCache(createInfo.m_ArtifactCache),
		m_Store(createInfo.m_CacheDirectory)
	{}

	IBLDerivedDataLookupResult IBLDerivedDataSystem::Lookup(
		const AssetContentFingerprint& contentFingerprint,
		const IBLBakeConfig& config,
		bool ignoreCache,
		std::stop_token stopToken) noexcept
	{
		IBLDerivedDataLookupResult result{};
		result.m_Key = BuildKey(contentFingerprint, config, stopToken);
		if (!result.m_Key.IsValid() || stopToken.stop_requested() || ignoreCache)
		{
			return result;
		}

		{
			std::scoped_lock lock(m_ArtifactMutex);
			const auto mapped = m_KeyToContentDigest.find(result.m_Key);
			result.m_Artifact = m_ArtifactCache.Find(
				mapped != m_KeyToContentDigest.end() ?
					mapped->second : ArtifactContentDigest{});
			if (result.m_Artifact && result.m_Artifact->MatchesConfig(config))
			{
				result.m_Source = IBLDerivedDataSource::CpuCache;
				return result;
			}
			result.m_Artifact.reset();
		}

		DerivedDataReadResult read = m_Store.Read(
			result.m_Key,
			IBLBundleArtifactType,
			IBLBundleArtifactSchemaVersion);
		if (stopToken.stop_requested() || read.m_Disposition != DerivedDataReadDisposition::Hit)
		{
			return result;
		}
		IBLBundleArtifactDecodeResult decoded = IBLBundleArtifactCodec::Deserialize(
			read.m_Payload,
			read.m_ArtifactContentDigest);
		if (!decoded.Succeeded() || !decoded.m_Artifact.MatchesConfig(config))
		{
			m_Store.DiscardCorrupt(result.m_Key);
			result.m_Error = decoded.m_Error.empty() ?
				"IBL bundle DDC artifact does not match its bake configuration." :
				std::move(decoded.m_Error);
			return result;
		}
		result.m_Artifact = Admit(
			result.m_Key,
			std::make_shared<const IBLBundleArtifact>(std::move(decoded.m_Artifact)));
		result.m_Source = result.m_Artifact ?
			IBLDerivedDataSource::LocalDdc : IBLDerivedDataSource::Miss;
		return result;
	}

	IBLBundleArtifactHandle IBLDerivedDataSystem::Admit(
		const DerivedDataKey& key,
		IBLBundleArtifactHandle artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return {};
		}
		std::scoped_lock lock(m_ArtifactMutex);
		const ArtifactContentDigest contentDigest = artifact->m_ContentDigest;
		artifact = m_ArtifactCache.Admit(std::move(artifact));
		if (artifact && m_ArtifactCache.Contains(contentDigest))
		{
			m_KeyToContentDigest[key] = contentDigest;
		}
		else
		{
			m_KeyToContentDigest.erase(key);
		}
		return artifact;
	}

	bool IBLDerivedDataSystem::Store(
		const DerivedDataKey& key,
		const IBLBundleArtifactHandle& artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return false;
		}
		const std::vector<std::byte> payload = IBLBundleArtifactCodec::Serialize(*artifact);
		return !payload.empty() && m_Store.Write(
			key,
			IBLBundleArtifactType,
			IBLBundleArtifactSchemaVersion,
			artifact->m_ContentDigest,
			payload);
	}

	IBLBundleArtifactCacheStatistics
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
		m_KeyToContentDigest.clear();
	}

	void IBLDerivedDataSystem::ClearStore() noexcept
	{
		m_Store.Clear();
	}

	DerivedDataKey IBLDerivedDataSystem::BuildKey(
		const AssetContentFingerprint& contentFingerprint,
		const IBLBakeConfig& config,
		std::stop_token stopToken) const noexcept
	{
		const SourceDigest shaderDigest = ComputeShaderDependencyDigest(stopToken);
		if (!contentFingerprint.IsValid() || !shaderDigest.IsValid() ||
			stopToken.stop_requested() ||
			(m_Compatibility == IBLArtifactCompatibility::AdapterScoped &&
				m_AdapterScopeIdentity.empty()))
		{
			return {};
		}
		DerivedDataKeyBuilder builder;
		bool succeeded = builder.AddStringUtf8(IBLBundleArtifactType);
		succeeded &= builder.AddU32(IBLBundleArtifactSchemaVersion);
		succeeded &= builder.AddU32(TextureArtifactSchemaVersion);
		succeeded &= builder.AddU32(IBLBundleProducerCompatibilityVersion);
		succeeded &= builder.AddU32(IBLBakeAlgorithmVersion);
		succeeded &= builder.AddU32(static_cast<uint32_t>(m_Compatibility));
		if (m_Compatibility == IBLArtifactCompatibility::AdapterScoped)
		{
			succeeded &= builder.AddStringUtf8(m_AdapterScopeIdentity);
		}
		succeeded &= builder.AddU64(contentFingerprint.m_SourceContentHash);
		succeeded &= builder.AddU64(contentFingerprint.m_ImportSettingsHash);
		succeeded &= builder.AddU32(contentFingerprint.m_DecoderVersion);
		succeeded &= builder.AddSourceDigest(shaderDigest);
		succeeded &= builder.AddU32(config.m_EnvironmentCubemapSize);
		succeeded &= builder.AddU32(static_cast<uint32_t>(config.m_EnvironmentCubemapFormat));
		succeeded &= builder.AddU32(config.m_IrradianceCubemapSize);
		succeeded &= builder.AddU32(static_cast<uint32_t>(config.m_IrradianceCubemapFormat));
		succeeded &= builder.AddU32(config.m_IrradianceSampleCount);
		succeeded &= builder.AddU32(config.m_PrefilteredSpecularCubemapSize);
		succeeded &= builder.AddU32(config.m_PrefilteredSpecularMipLevels);
		succeeded &= builder.AddU32(static_cast<uint32_t>(
			config.m_PrefilteredSpecularCubemapFormat));
		succeeded &= builder.AddU32(config.m_PrefilteredSpecularSampleCount);
		succeeded &= builder.AddU32(std::bit_cast<uint32_t>(
			config.m_PrefilteredSpecularMaxSampleLuminance));
		succeeded &= builder.AddU32(config.m_BrdfLutSize);
		succeeded &= builder.AddU32(static_cast<uint32_t>(config.m_BrdfLutFormat));
		return succeeded ? builder.Finish() : DerivedDataKey{};
	}

	SourceDigest IBLDerivedDataSystem::ComputeShaderDependencyDigest(
		std::stop_token stopToken) const noexcept
	{
		Sha256Builder builder;
		bool succeeded = builder.IsValid();
		const std::filesystem::path shaderRoot = GetShaderSourceRoot();
		std::set<std::filesystem::path> visited;
		for (std::string_view shaderPath : BakeShaderPaths)
		{
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
