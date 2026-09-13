#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace gglab
{
	struct SourceDigest
	{
		std::array<std::byte, 32> m_Value{};
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			for (const std::byte value : m_Value)
			{
				if (value != std::byte{})
					return true;
			}
			return false;
		}
		friend constexpr bool operator==(
			const SourceDigest&, const SourceDigest&) noexcept = default;
	};

	struct DerivedDataKey
	{
		std::array<std::byte, 32> m_Value{};
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			for (const std::byte value : m_Value)
			{
				if (value != std::byte{})
					return true;
			}
			return false;
		}
		friend constexpr bool operator==(
			const DerivedDataKey&, const DerivedDataKey&) noexcept = default;
	};

	[[nodiscard]] std::string SourceDigestText(const SourceDigest& digest, size_t byteCount = 8);
	[[nodiscard]] std::string DerivedDataKeyText(const DerivedDataKey& key, size_t byteCount = 8);
}
