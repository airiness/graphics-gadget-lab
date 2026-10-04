#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetCacheStatistics.h"
#include "GGLabRuntime/Graphics/Asset/TextureDerivedDataTypes.h"

#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

namespace gglab
{
	struct TextureDerivedDataCoordinatorCore;
	struct TextureDerivedDataStoreState;

	class TextureDerivedDataSystem final
	{
	public:
		explicit TextureDerivedDataSystem(std::filesystem::path cacheDirectory) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureDerivedDataSystem);
		~TextureDerivedDataSystem();

		[[nodiscard]] TextureDerivedDataRequestResult Request(
			const DerivedDataKey& key) noexcept;
		[[nodiscard]] TextureArtifactWaitResult Wait(
			TextureArtifactWaiterHandle waiter, std::stop_token stopToken) noexcept;
		[[nodiscard]] bool Publish(
			TextureArtifactBuildClaim claim, TextureDerivedDataArtifact artifact) noexcept;
		[[nodiscard]] bool Fail(TextureArtifactBuildClaim claim, std::string error) noexcept;

		[[nodiscard]] TextureDerivedDataArtifact Read(
			const DerivedDataKey& key, const TextureImportSettings& importSettings) noexcept;
		[[nodiscard]] bool Write(
			const DerivedDataKey& key, const TextureArtifact& artifact) noexcept;

		[[nodiscard]] TextureDerivedDataCoordinatorStatistics GetCoordinatorStatistics()
			const noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStoreStatistics() const noexcept;
		[[nodiscard]] bool Contains(const DerivedDataKey& key) const noexcept;
		[[nodiscard]] bool Clear() noexcept;

	private:
		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		std::unique_ptr<TextureDerivedDataStoreState> m_Store;
	};
}
