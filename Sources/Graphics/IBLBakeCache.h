#pragma once
#include "Graphics/IBLBakeTypes.h"
#include "Graphics/TextureAsset.h"

#include <filesystem>
#include <stop_token>

namespace gglab
{
	struct IBLBakeCachePayload
	{
		TextureAssetData m_Environment;
		TextureAssetData m_Irradiance;
		TextureAssetData m_PrefilteredSpecular;
		TextureAssetData m_BrdfLut;
	};

	class IBLBakeCache
	{
	public:
		static constexpr uint64_t CacheFormatVersion = 1;
		static constexpr uint64_t BakeAlgorithmVersion = 2;

		explicit IBLBakeCache(std::filesystem::path rootDirectory) noexcept;

		[[nodiscard]] uint64_t ComputeKey(
			const std::filesystem::path& environmentPath,
			const IBLBakeConfig& config,
			std::stop_token stopToken = {}) const noexcept;
		[[nodiscard]] bool TryLoad(
			uint64_t key,
			const IBLBakeConfig& config,
			IBLBakeCachePayload& outPayload,
			std::stop_token stopToken = {}) const noexcept;
		[[nodiscard]] bool Store(uint64_t key, const IBLBakeCachePayload& payload) const noexcept;

		[[nodiscard]] const std::filesystem::path& GetRootDirectory() const noexcept { return m_RootDirectory; }

	private:
		[[nodiscard]] std::filesystem::path GetEntryDirectory(uint64_t key) const;

		std::filesystem::path m_RootDirectory;
	};
}
