#include "Core/Hash/Sha256.h"

#include <Windows.h>

#include <algorithm>
#include <bcrypt.h>
#include <limits>
#include <type_traits>

#pragma comment(lib, "bcrypt.lib")

namespace gglab
{
	namespace
	{
		template <class T>
		[[nodiscard]] std::array<std::byte, sizeof(T)> EncodeLittleEndian(T value) noexcept
		{
			static_assert(std::is_unsigned_v<T>);
			std::array<std::byte, sizeof(T)> bytes{};
			for (size_t index = 0; index < bytes.size(); ++index)
			{
				bytes[index] = static_cast<std::byte>(value & 0xffu);
				value >>= 8;
			}
			return bytes;
		}
	}

	Sha256Builder::Sha256Builder() noexcept
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		if (!BCRYPT_SUCCESS(
			BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
		{
			return;
		}
		m_Algorithm = algorithm;

		DWORD objectBytes = 0;
		DWORD resultBytes = 0;
		if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &resultBytes, 0)))
		{
			return;
		}
		m_Object.resize(objectBytes);
		BCRYPT_HASH_HANDLE hash = nullptr;
		if (BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, m_Object.data(),
			static_cast<ULONG>(m_Object.size()), nullptr, 0, 0)))
		{
			m_Hash = hash;
		}
	}

	Sha256Builder::~Sha256Builder()
	{
		if (m_Hash)
		{
			BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(m_Hash));
		}
		if (m_Algorithm)
		{
			BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_Algorithm), 0);
		}
	}

	bool Sha256Builder::IsValid() const noexcept
	{
		return m_Hash != nullptr;
	}

	bool Sha256Builder::AddBytes(std::span<const std::byte> bytes) noexcept
	{
		if (!m_Hash)
		{
			return false;
		}
		while (!bytes.empty())
		{
			const size_t chunkSize =
				std::min<size_t>(bytes.size(), std::numeric_limits<ULONG>::max());
			if (!BCRYPT_SUCCESS(BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(m_Hash),
				reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
				static_cast<ULONG>(chunkSize), 0)))
			{
				return false;
			}
			bytes = bytes.subspan(chunkSize);
		}
		return true;
	}

	bool Sha256Builder::AddU8(uint8_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddU16(uint16_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddU32(uint32_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddU64(uint64_t value) noexcept
	{
		return AddBytes(EncodeLittleEndian(value));
	}

	bool Sha256Builder::AddStringUtf8(std::string_view value) noexcept
	{
		return AddU64(static_cast<uint64_t>(value.size())) &&
			AddBytes(std::as_bytes(std::span{ value.data(), value.size() }));
	}

	Sha256Hash Sha256Builder::Finish() noexcept
	{
		Sha256Hash result{};
		if (!m_Hash || !BCRYPT_SUCCESS(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(m_Hash),
			reinterpret_cast<PUCHAR>(result.m_Value.data()),
			static_cast<ULONG>(result.m_Value.size()), 0)))
		{
			return {};
		}
		return result;
	}

	Sha256Hash ComputeSha256(std::span<const std::byte> bytes) noexcept
	{
		Sha256Builder builder;
		if (!builder.IsValid() || !builder.AddBytes(bytes))
		{
			return {};
		}
		return builder.Finish();
	}
}
