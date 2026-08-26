#include "Platform/Win/Hash/Win32Sha256Backend.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <new>

#pragma comment(lib, "bcrypt.lib")

namespace gglab::foundation::detail
{
	namespace
	{
		class BCryptSha256Provider final
		{
		public:
			BCryptSha256Provider() noexcept
			{
				if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
					&m_Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
				{
					m_Algorithm = nullptr;
					return;
				}
				DWORD resultBytes = 0;
				if (!BCRYPT_SUCCESS(BCryptGetProperty(m_Algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&m_ObjectBytes), sizeof(m_ObjectBytes),
					&resultBytes, 0)))
				{
					BCryptCloseAlgorithmProvider(m_Algorithm, 0);
					m_Algorithm = nullptr;
					m_ObjectBytes = 0;
				}
			}

			~BCryptSha256Provider()
			{
				if (m_Algorithm)
				{
					BCryptCloseAlgorithmProvider(m_Algorithm, 0);
				}
			}

			GGLAB_DELETE_COPYABLE_MOVABLE(BCryptSha256Provider);

			[[nodiscard]] bool IsValid() const noexcept
			{
				return m_Algorithm != nullptr && m_ObjectBytes > 0;
			}
			[[nodiscard]] BCRYPT_ALG_HANDLE Get() const noexcept { return m_Algorithm; }
			[[nodiscard]] ULONG GetObjectBytes() const noexcept { return m_ObjectBytes; }

		private:
			BCRYPT_ALG_HANDLE m_Algorithm = nullptr;
			ULONG m_ObjectBytes = 0;
		};

		[[nodiscard]] const BCryptSha256Provider& GetProvider() noexcept
		{
			static const BCryptSha256Provider provider;
			return provider;
		}

		class Win32Sha256Backend final : public Sha256Backend
		{
		public:
			Win32Sha256Backend() noexcept
			{
				const BCryptSha256Provider& provider = GetProvider();
				if (!provider.IsValid())
				{
					return;
				}
				m_Object.reset(new (std::nothrow) UCHAR[provider.GetObjectBytes()]);
				if (!m_Object)
				{
					return;
				}
				if (!BCRYPT_SUCCESS(BCryptCreateHash(provider.Get(), &m_Hash, m_Object.get(),
					provider.GetObjectBytes(), nullptr, 0, 0)))
				{
					m_Hash = nullptr;
				}
			}

			~Win32Sha256Backend() override
			{
				DestroyHash();
			}

			[[nodiscard]] bool IsValid() const noexcept override
			{
				return m_Hash != nullptr && !m_Finished && !m_Failed;
			}

			bool AddBytes(std::span<const std::byte> bytes) noexcept override
			{
				if (!IsValid())
				{
					m_Failed = true;
					return false;
				}
				while (!bytes.empty())
				{
					const std::size_t chunkBytes =
						std::min<std::size_t>(bytes.size(), std::numeric_limits<ULONG>::max());
					if (!BCRYPT_SUCCESS(BCryptHashData(m_Hash,
						reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
						static_cast<ULONG>(chunkBytes), 0)))
					{
						m_Failed = true;
						return false;
					}
					bytes = bytes.subspan(chunkBytes);
				}
				return true;
			}

			[[nodiscard]] Sha256Digest Finish() noexcept override
			{
				if (!IsValid())
				{
					m_Failed = true;
					return {};
				}
				Sha256Digest result{};
				const bool succeeded = BCRYPT_SUCCESS(BCryptFinishHash(m_Hash,
					reinterpret_cast<PUCHAR>(result.m_Value.data()),
					static_cast<ULONG>(result.m_Value.size()), 0));
				m_Finished = true;
				m_Failed = !succeeded;
				DestroyHash();
				return succeeded ? result : Sha256Digest{};
			}

		private:
			void DestroyHash() noexcept
			{
				if (m_Hash)
				{
					BCryptDestroyHash(m_Hash);
					m_Hash = nullptr;
				}
			}

			std::unique_ptr<UCHAR[]> m_Object;
			BCRYPT_HASH_HANDLE m_Hash = nullptr;
			bool m_Finished = false;
			bool m_Failed = false;
		};
	}

	std::unique_ptr<Sha256Backend> CreateWin32Sha256Backend() noexcept
	{
		auto backend = std::unique_ptr<Win32Sha256Backend>(
			new (std::nothrow) Win32Sha256Backend);
		if (!backend || !backend->IsValid())
		{
			return {};
		}
		return backend;
	}
}
