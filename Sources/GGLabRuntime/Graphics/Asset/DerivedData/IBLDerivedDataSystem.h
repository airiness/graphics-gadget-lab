#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Asset/AssetContentFingerprint.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Graphics/Asset/IBLStageArtifactCache.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <array>
#include <filesystem>
#include <mutex>
#include <stop_token>

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

	struct IBLStageDerivedDataLookupResult
	{
		DerivedDataKey m_Key{};
		IBLStageArtifactHandle m_Artifact;
		IBLDerivedDataSource m_Source = IBLDerivedDataSource::Miss;
		std::string m_Error;
	};

	struct IBLDerivedDataLookupResult
	{
		std::array<IBLStageDerivedDataLookupResult, static_cast<size_t>(IBLArtifactStage::Count)>
			m_Stages{};

		[[nodiscard]] IBLStageDerivedDataLookupResult& Get(IBLArtifactStage stage) noexcept
		{
			return m_Stages[static_cast<size_t>(stage)];
		}
		[[nodiscard]] const IBLStageDerivedDataLookupResult& Get(
			IBLArtifactStage stage) const noexcept
		{
			return m_Stages[static_cast<size_t>(stage)];
		}
	};

	class IBLDerivedDataSystem final
	{
	public:
		struct CreateInfo
		{
			std::filesystem::path m_CacheDirectory;
			IBLStageArtifactCacheConfig m_ArtifactCache{};
			IBLArtifactCompatibility m_Compatibility = IBLArtifactCompatibility::AdapterScoped;
			std::string m_AdapterScopeIdentity;
		};

		explicit IBLDerivedDataSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(IBLDerivedDataSystem);
		~IBLDerivedDataSystem() = default;

		[[nodiscard]] IBLDerivedDataLookupResult Lookup(
			const AssetContentFingerprint& contentFingerprint,
			EnvironmentTextureSourceType sourceType, const IBLBakeConfig& config,
			const ShaderProgramRegistryArtifactRef& shaderRegistryRef, bool ignoreCache,
			std::stop_token stopToken = {}) noexcept;
		[[nodiscard]] IBLStageArtifactHandle Admit(
			const DerivedDataKey& key, IBLStageArtifactHandle artifact) noexcept;
		[[nodiscard]] bool Store(
			const DerivedDataKey& key, const IBLStageArtifactHandle& artifact) noexcept;

		[[nodiscard]] IBLStageArtifactCacheStatistics GetArtifactCacheStatistics() const noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStoreStatistics() const noexcept;
		void ClearArtifactCache() noexcept;
		[[nodiscard]] bool ClearStore() noexcept;

	private:
		[[nodiscard]] std::array<DerivedDataKey, static_cast<size_t>(IBLArtifactStage::Count)>
			BuildKeys(const AssetContentFingerprint& contentFingerprint,
				EnvironmentTextureSourceType sourceType, const IBLBakeConfig& config,
				const ShaderProgramRegistryArtifactRef& shaderRegistryRef,
				std::stop_token stopToken) const noexcept;

		IBLArtifactCompatibility m_Compatibility = IBLArtifactCompatibility::AdapterScoped;
		std::string m_AdapterScopeIdentity;
		mutable std::mutex m_ArtifactMutex;
		IBLStageArtifactCache m_ArtifactCache;
		LocalDerivedDataStore m_Store;
	};

	[[nodiscard]] std::string_view GetIBLStageArtifactType(IBLArtifactStage stage) noexcept;
}
