#pragma once
#include "GGLabRuntime/Graphics/Asset/TextureDerivedDataAcceptance.h"

#include <filesystem>
#include <memory>
#include <stop_token>

namespace gglab
{
	struct TextureDerivedDataCoordinatorCore;
	struct TextureDerivedDataStoreState;

	class TextureDerivedDataSystem final : public TextureDerivedDataAcceptance
	{
	public:
		explicit TextureDerivedDataSystem(std::filesystem::path cacheDirectory) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureDerivedDataSystem);
		~TextureDerivedDataSystem() override;

		[[nodiscard]] TextureDerivedDataRequestResult Request(
			const DerivedDataKey& key) noexcept override;
		[[nodiscard]] TextureArtifactWaitResult Wait(
			TextureArtifactWaiterHandle waiter, std::stop_token stopToken) noexcept override;
		[[nodiscard]] bool Publish(
			TextureArtifactBuildClaim claim, TextureDerivedDataArtifact artifact) noexcept override;
		[[nodiscard]] bool Fail(TextureArtifactBuildClaim claim, std::string error) noexcept override;

		[[nodiscard]] TextureDerivedDataArtifact Read(
			const DerivedDataKey& key, const TextureImportSettings& importSettings) noexcept;
		[[nodiscard]] bool Write(
			const DerivedDataKey& key, const TextureArtifact& artifact) noexcept;

		[[nodiscard]] TextureDerivedDataCoordinatorStatistics GetCoordinatorStatistics()
			const noexcept override;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStoreStatistics() const noexcept;
		[[nodiscard]] bool Contains(const DerivedDataKey& key) const noexcept;
		[[nodiscard]] bool Clear() noexcept;

	private:
		std::shared_ptr<TextureDerivedDataCoordinatorCore> m_Core;
		std::unique_ptr<TextureDerivedDataStoreState> m_Store;
	};
}
