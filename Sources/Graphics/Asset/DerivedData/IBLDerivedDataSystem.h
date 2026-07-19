#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/AssetContentFingerprint.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Graphics/Asset/IBLBundleArtifactCache.h"

#include <filesystem>
#include <mutex>
#include <stop_token>
#include <unordered_map>

namespace gglab
{
	enum class IBLArtifactCompatibility : uint8_t
	{
		Portable,
		AdapterScoped,
	};

	enum class IBLDerivedDataSource : uint8_t
	{
		Miss,
		CpuCache,
		LocalDdc,
	};

	struct IBLDerivedDataLookupResult
	{
		DerivedDataKey m_Key{};
		IBLBundleArtifactHandle m_Artifact;
		IBLDerivedDataSource m_Source = IBLDerivedDataSource::Miss;
		std::string m_Error;
	};

	class IBLDerivedDataSystem final
	{
	public:
		struct CreateInfo
		{
			std::filesystem::path m_CacheDirectory;
			IBLBundleArtifactCacheConfig m_ArtifactCache{};
			IBLArtifactCompatibility m_Compatibility = IBLArtifactCompatibility::AdapterScoped;
			std::string m_AdapterScopeIdentity;
		};

		explicit IBLDerivedDataSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(IBLDerivedDataSystem);
		~IBLDerivedDataSystem() = default;

		[[nodiscard]] IBLDerivedDataLookupResult Lookup(
			const AssetContentFingerprint& contentFingerprint,
			const IBLBakeConfig& config,
			bool ignoreCache,
			std::stop_token stopToken = {}) noexcept;
		[[nodiscard]] IBLBundleArtifactHandle Admit(
			const DerivedDataKey& key,
			IBLBundleArtifactHandle artifact) noexcept;
		[[nodiscard]] bool Store(
			const DerivedDataKey& key,
			const IBLBundleArtifactHandle& artifact) noexcept;

		[[nodiscard]] IBLBundleArtifactCacheStatistics GetArtifactCacheStatistics() const noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStoreStatistics() const noexcept;
		void ClearArtifactCache() noexcept;
		void ClearStore() noexcept;

	private:
		[[nodiscard]] DerivedDataKey BuildKey(
			const AssetContentFingerprint& contentFingerprint,
			const IBLBakeConfig& config,
			std::stop_token stopToken) const noexcept;
		[[nodiscard]] SourceDigest ComputeShaderDependencyDigest(
			std::stop_token stopToken) const noexcept;

		IBLArtifactCompatibility m_Compatibility = IBLArtifactCompatibility::AdapterScoped;
		std::string m_AdapterScopeIdentity;
		mutable std::mutex m_ArtifactMutex;
		IBLBundleArtifactCache m_ArtifactCache;
		std::unordered_map<DerivedDataKey, ArtifactContentDigest, DerivedDataKeyHash>
			m_KeyToContentDigest;
		LocalDerivedDataStore m_Store;
	};
}
