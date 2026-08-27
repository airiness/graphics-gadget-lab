#include "GGLabFoundation/Hash/Sha256.h"
#include "Hash/Sha256Backend.h"

#include <algorithm>
#include <functional>
#include <new>
#include <type_traits>

namespace gglab
{
	namespace
	{
		template <class T>
		[[nodiscard]] std::array<std::byte, sizeof(T)> EncodeLittleEndian(T value) noexcept
		{
			static_assert(std::is_unsigned_v<T>);
			std::array<std::byte, sizeof(T)> bytes{};
			for (std::size_t index = 0; index < bytes.size(); ++index)
			{
				bytes[index] = static_cast<std::byte>(value & 0xffu);
				value >>= 8;
			}
			return bytes;
		}
	}

	struct Sha256Builder::Implementation final
	{
		std::unique_ptr<foundation::detail::Sha256Backend> m_Backend;
	};

	std::size_t Sha256DigestHash::operator()(const Sha256Digest& digest) const noexcept
	{
		const std::string_view bytes(
			reinterpret_cast<const char*>(digest.m_Value.data()), digest.m_Value.size());
		return std::hash<std::string_view>{}(bytes);
	}

	std::string Sha256DigestToHex(const Sha256Digest& digest, std::size_t byteCount)
	{
		if (!digest.IsValid())
		{
			return {};
		}

		constexpr char HexDigits[] = "0123456789abcdef";
		const std::size_t count = std::min(byteCount, digest.m_Value.size());
		std::string result(count * 2, '0');
		for (std::size_t index = 0; index < count; ++index)
		{
			const std::uint8_t value = std::to_integer<std::uint8_t>(digest.m_Value[index]);
			result[index * 2] = HexDigits[value >> 4];
			result[index * 2 + 1] = HexDigits[value & 0x0fu];
		}
		return result;
	}

	Sha256Builder::Sha256Builder() noexcept :
		m_Implementation(new (std::nothrow) Implementation)
	{
		if (m_Implementation)
		{
			m_Implementation->m_Backend = foundation::detail::CreateSha256Backend();
		}
	}

	Sha256Builder::~Sha256Builder() = default;

	bool Sha256Builder::IsValid() const noexcept
	{
		return m_Implementation && m_Implementation->m_Backend &&
			m_Implementation->m_Backend->IsValid();
	}

	bool Sha256Builder::AddBytes(std::span<const std::byte> bytes) noexcept
	{
		return IsValid() && m_Implementation->m_Backend->AddBytes(bytes);
	}

	bool Sha256Builder::AddU8(std::uint8_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddU16LE(std::uint16_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddU32LE(std::uint32_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddU64LE(std::uint64_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddStringUtf8(std::string_view value) noexcept
	{
		return AddU64LE(static_cast<std::uint64_t>(value.size())) &&
			AddBytes(std::as_bytes(std::span{ value.data(), value.size() }));
	}

	Sha256Digest Sha256Builder::Finish() noexcept
	{
		return IsValid() ? m_Implementation->m_Backend->Finish() : Sha256Digest{};
	}

	Sha256Digest ComputeSha256(std::span<const std::byte> bytes) noexcept
	{
		Sha256Builder builder;
		if (!builder.AddBytes(bytes))
		{
			return {};
		}
		return builder.Finish();
	}
}
