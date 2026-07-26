#include "NapaVoxelCore/Hash/CanonicalHash.h"

#include <bit>
#include <cstdint>

namespace napa::voxel
{
	void CanonicalHashWriter::WriteU8(std::uint8_t value) noexcept
	{
		WriteByte(value);
	}

	void CanonicalHashWriter::WriteU16(std::uint16_t value) noexcept
	{
		WriteByte(static_cast<std::uint8_t>(value));
		WriteByte(static_cast<std::uint8_t>(value >> 8));
	}

	void CanonicalHashWriter::WriteU32(std::uint32_t value) noexcept
	{
		WriteByte(static_cast<std::uint8_t>(value));
		WriteByte(static_cast<std::uint8_t>(value >> 8));
		WriteByte(static_cast<std::uint8_t>(value >> 16));
		WriteByte(static_cast<std::uint8_t>(value >> 24));
	}

	void CanonicalHashWriter::WriteU64(std::uint64_t value) noexcept
	{
		WriteByte(static_cast<std::uint8_t>(value));
		WriteByte(static_cast<std::uint8_t>(value >> 8));
		WriteByte(static_cast<std::uint8_t>(value >> 16));
		WriteByte(static_cast<std::uint8_t>(value >> 24));
		WriteByte(static_cast<std::uint8_t>(value >> 32));
		WriteByte(static_cast<std::uint8_t>(value >> 40));
		WriteByte(static_cast<std::uint8_t>(value >> 48));
		WriteByte(static_cast<std::uint8_t>(value >> 56));
	}

	void CanonicalHashWriter::WriteI32(std::int32_t value) noexcept
	{
		WriteU32(static_cast<std::uint32_t>(value));
	}

	void CanonicalHashWriter::WriteI64(std::int64_t value) noexcept
	{
		WriteU64(static_cast<std::uint64_t>(value));
	}

	void CanonicalHashWriter::WriteFloat32(float value) noexcept
	{
		const float canonical = value == 0.0f ? 0.0f : value;
		WriteU32(std::bit_cast<std::uint32_t>(canonical));
	}

	void CanonicalHashWriter::WriteFloat64(double value) noexcept
	{
		const double canonical = value == 0.0 ? 0.0 : value;
		WriteU64(std::bit_cast<std::uint64_t>(canonical));
	}

	void CanonicalHashWriter::WriteCount(std::uint32_t value) noexcept
	{
		WriteU32(value);
	}

	void CanonicalHashWriter::WriteCount(std::uint64_t value) noexcept
	{
		WriteU64(value);
	}

	std::uint64_t CanonicalHashWriter::GetValue() const noexcept
	{
		return m_Value;
	}

	void CanonicalHashWriter::WriteByte(std::uint8_t value) noexcept
	{
		m_Value = (m_Value ^ value) * Fnv1a64Prime;
	}
}
