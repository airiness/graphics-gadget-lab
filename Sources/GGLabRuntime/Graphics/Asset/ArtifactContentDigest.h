#pragma once
#include "GGLabRuntime/Core/Hash/KeyHash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gglab
{
	struct ArtifactContentDigest
	{
		std::array<std::byte, 32> m_Value{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			for (const std::byte value : m_Value)
			{
				if (value != std::byte{})
				{
					return true;
				}
			}
			return false;
		}

		friend constexpr bool operator==(
			const ArtifactContentDigest&, const ArtifactContentDigest&) noexcept = default;
	};

	struct ArtifactContentDigestHash
	{
		[[nodiscard]] size_t operator()(const ArtifactContentDigest& digest) const noexcept
		{
			return FNV1a64::HashBytes64(digest.m_Value.data(), digest.m_Value.size());
		}
	};

	[[nodiscard]] std::string ArtifactContentDigestText(
		const ArtifactContentDigest& digest, size_t byteCount = 8);
}
