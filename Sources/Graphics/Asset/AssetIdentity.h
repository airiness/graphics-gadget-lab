#pragma once
#include "Core/Hash/KeyHash.h"

#include <cstdint>
#include <tuple>

namespace gglab
{
	enum class AssetKind : uint8_t
	{
		Unknown,
		Model,
		Texture,
		Mesh,
		Material,
	};

	struct AssetKey
	{
		AssetKind m_Kind = AssetKind::Unknown;
		uint64_t m_StableId = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Kind != AssetKind::Unknown;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::tie(m_Kind, m_StableId);
		}

		friend constexpr bool operator==(const AssetKey&, const AssetKey&) = default;
	};
	using AssetKeyHash = KeyHash<AssetKey>;

	struct AssetContentVersion
	{
		AssetKey m_Key{};
		uint64_t m_ContentGeneration = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Key.IsValid() && m_ContentGeneration != 0;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::tie(
				m_Key.m_Kind,
				m_Key.m_StableId,
				m_ContentGeneration);
		}

		friend constexpr bool operator==(
			const AssetContentVersion&,
			const AssetContentVersion&) = default;
	};
	using AssetContentVersionHash = KeyHash<AssetContentVersion>;

	struct AssetOperationToken
	{
		AssetContentVersion m_ContentVersion{};
		uint64_t m_OperationSerial = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_ContentVersion.IsValid() && m_OperationSerial != 0;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::tie(
				m_ContentVersion.m_Key.m_Kind,
				m_ContentVersion.m_Key.m_StableId,
				m_ContentVersion.m_ContentGeneration,
				m_OperationSerial);
		}

		friend constexpr bool operator==(
			const AssetOperationToken&,
			const AssetOperationToken&) = default;
	};
	using AssetOperationTokenHash = KeyHash<AssetOperationToken>;

	[[nodiscard]] constexpr AssetKey MakeAssetKey(
		AssetKind kind,
		uint64_t stableId) noexcept
	{
		return kind != AssetKind::Unknown ?
			AssetKey{ .m_Kind = kind, .m_StableId = stableId } : AssetKey{};
	}

	[[nodiscard]] constexpr AssetContentVersion MakeAssetContentVersion(
		AssetKey key,
		uint64_t contentGeneration) noexcept
	{
		return {
			.m_Key = key,
			.m_ContentGeneration = contentGeneration,
		};
	}

	[[nodiscard]] constexpr AssetContentVersion MakeAssetContentVersion(
		AssetKind kind,
		uint64_t stableId,
		uint64_t contentGeneration) noexcept
	{
		return MakeAssetContentVersion(
			MakeAssetKey(kind, stableId),
			contentGeneration);
	}

	[[nodiscard]] constexpr AssetOperationToken MakeAssetOperationToken(
		AssetContentVersion contentVersion,
		uint64_t operationSerial) noexcept
	{
		return {
			.m_ContentVersion = contentVersion,
			.m_OperationSerial = operationSerial,
		};
	}
}
