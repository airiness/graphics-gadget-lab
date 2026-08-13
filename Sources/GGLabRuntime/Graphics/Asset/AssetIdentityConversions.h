#pragma once
#include "Graphics/Asset/AssetIdentity.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	[[nodiscard]] constexpr AssetKey MakeAssetKey(ModelID id) noexcept
	{
		return id.IsValid() ? MakeAssetKey(AssetKind::Model, id.Value()) : AssetKey{};
	}

	[[nodiscard]] constexpr AssetKey MakeAssetKey(TextureID id) noexcept
	{
		return id.IsValid() ? MakeAssetKey(AssetKind::Texture, id.Value()) : AssetKey{};
	}

	[[nodiscard]] constexpr AssetKey MakeAssetKey(MeshID id) noexcept
	{
		return id.IsValid() ? MakeAssetKey(AssetKind::Mesh, id.Value()) : AssetKey{};
	}

	[[nodiscard]] constexpr AssetContentVersion MakeAssetContentVersion(
		ModelID id, uint64_t contentGeneration) noexcept
	{
		return MakeAssetContentVersion(MakeAssetKey(id), contentGeneration);
	}

	[[nodiscard]] constexpr AssetContentVersion MakeAssetContentVersion(
		TextureID id, uint64_t contentGeneration) noexcept
	{
		return MakeAssetContentVersion(MakeAssetKey(id), contentGeneration);
	}

	[[nodiscard]] constexpr AssetContentVersion MakeAssetContentVersion(
		MeshID id, uint64_t contentGeneration) noexcept
	{
		return MakeAssetContentVersion(MakeAssetKey(id), contentGeneration);
	}

}
