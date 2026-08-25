#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	struct Sha256Digest final
	{
		static constexpr std::size_t Size = 32;

		std::array<std::byte, Size> m_Value{};

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
			const Sha256Digest&, const Sha256Digest&) noexcept = default;
	};

	struct Sha256DigestHash final
	{
		// Container hash only. Do not persist this value as artifact identity.
		[[nodiscard]] std::size_t operator()(const Sha256Digest& digest) const noexcept;
	};

	[[nodiscard]] std::string Sha256DigestToHex(
		const Sha256Digest& digest, std::size_t byteCount = Sha256Digest::Size);

	class Sha256Builder final
	{
	public:
		Sha256Builder() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Sha256Builder);
		~Sha256Builder();

		[[nodiscard]] bool IsValid() const noexcept;
		bool AddBytes(std::span<const std::byte> bytes) noexcept;
		bool AddU8(std::uint8_t value) noexcept;
		bool AddU16LE(std::uint16_t value) noexcept;
		bool AddU32LE(std::uint32_t value) noexcept;
		bool AddU64LE(std::uint64_t value) noexcept;

		// Stable encoding: U64LE byte length followed by the UTF-8 bytes.
		bool AddStringUtf8(std::string_view value) noexcept;
		[[nodiscard]] Sha256Digest Finish() noexcept;

	private:
		struct Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

	[[nodiscard]] Sha256Digest ComputeSha256(std::span<const std::byte> bytes) noexcept;
}
