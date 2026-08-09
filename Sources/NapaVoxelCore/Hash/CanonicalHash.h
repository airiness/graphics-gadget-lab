#pragma once

#include <cstdint>
#include <type_traits>

namespace napa::voxel
{
	inline constexpr std::uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
	inline constexpr std::uint64_t Fnv1a64Prime = 1099511628211ull;

	class CanonicalHashWriter final
	{
	public:
		CanonicalHashWriter() noexcept = default;

		void WriteU8(std::uint8_t value) noexcept;
		void WriteU16(std::uint16_t value) noexcept;
		void WriteI16(std::int16_t value) noexcept;
		void WriteU32(std::uint32_t value) noexcept;
		void WriteU64(std::uint64_t value) noexcept;
		void WriteI32(std::int32_t value) noexcept;
		void WriteI64(std::int64_t value) noexcept;
		void WriteFloat32(float value) noexcept;
		void WriteFloat64(double value) noexcept;
		void WriteCount(std::uint32_t value) noexcept;
		void WriteCount(std::uint64_t value) noexcept;

		template <typename Enum>
			requires std::is_enum_v<Enum>
		void WriteEnum(Enum value) noexcept
		{
			using Underlying = std::underlying_type_t<Enum>;
			using Unsigned = std::make_unsigned_t<Underlying>;

			const Unsigned bits = static_cast<Unsigned>(value);
			if constexpr (sizeof(Unsigned) == sizeof(std::uint8_t))
			{
				WriteU8(static_cast<std::uint8_t>(bits));
			}
			else if constexpr (sizeof(Unsigned) == sizeof(std::uint16_t))
			{
				WriteU16(static_cast<std::uint16_t>(bits));
			}
			else if constexpr (sizeof(Unsigned) == sizeof(std::uint32_t))
			{
				WriteU32(static_cast<std::uint32_t>(bits));
			}
			else
			{
				static_assert(sizeof(Unsigned) == sizeof(std::uint64_t));
				WriteU64(static_cast<std::uint64_t>(bits));
			}
		}

		[[nodiscard]] std::uint64_t GetValue() const noexcept;

	private:
		void WriteByte(std::uint8_t value) noexcept;

		std::uint64_t m_Value = Fnv1a64OffsetBasis;
	};
}
