#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gglab
{
	struct Sha256Hash
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
	};

	class Sha256Builder final
	{
	public:
		Sha256Builder() noexcept;
		Sha256Builder(const Sha256Builder&) = delete;
		Sha256Builder& operator=(const Sha256Builder&) = delete;
		~Sha256Builder();

		[[nodiscard]] bool IsValid() const noexcept;
		bool AddBytes(std::span<const std::byte> bytes) noexcept;
		bool AddU8(uint8_t value) noexcept;
		bool AddU16(uint16_t value) noexcept;
		bool AddU32(uint32_t value) noexcept;
		bool AddU64(uint64_t value) noexcept;
		bool AddStringUtf8(std::string_view value) noexcept;
		[[nodiscard]] Sha256Hash Finish() noexcept;

	private:
		void* m_Algorithm = nullptr;
		void* m_Hash = nullptr;
		std::vector<uint8_t> m_Object;
	};

	[[nodiscard]] Sha256Hash ComputeSha256(
		std::span<const std::byte> bytes) noexcept;
}
