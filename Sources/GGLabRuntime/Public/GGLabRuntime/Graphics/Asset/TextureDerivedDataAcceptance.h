#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetCacheStatistics.h"
#include "GGLabRuntime/Graphics/Asset/TextureDerivedDataTypes.h"

#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

namespace gglab
{
	// Narrow acceptance capability over the Runtime texture derived-data
	// coordinator. The concrete system and its storage remain Runtime-internal;
	// acceptance Labs validate coordinator semantics through this contract.
	class TextureDerivedDataAcceptance
	{
	public:
		virtual ~TextureDerivedDataAcceptance() = default;

		[[nodiscard]] virtual TextureDerivedDataRequestResult Request(
			const DerivedDataKey& key) noexcept = 0;
		[[nodiscard]] virtual TextureArtifactWaitResult Wait(
			TextureArtifactWaiterHandle waiter, std::stop_token stopToken) noexcept = 0;
		[[nodiscard]] virtual bool Publish(
			TextureArtifactBuildClaim claim, TextureDerivedDataArtifact artifact) noexcept = 0;
		[[nodiscard]] virtual bool Fail(
			TextureArtifactBuildClaim claim, std::string error) noexcept = 0;
		[[nodiscard]] virtual TextureDerivedDataCoordinatorStatistics GetCoordinatorStatistics()
			const noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<TextureDerivedDataAcceptance>
		CreateTextureDerivedDataAcceptance(std::filesystem::path cacheDirectory) noexcept;
}
