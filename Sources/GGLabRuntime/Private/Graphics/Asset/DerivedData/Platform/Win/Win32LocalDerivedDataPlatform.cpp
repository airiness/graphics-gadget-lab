#include "Graphics/Asset/DerivedData/Platform/Win/Win32LocalDerivedDataPlatform.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Platform/Win/Win32NamedMutex.h"
#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::string DigestText(const Sha256Digest& digest)
		{
			constexpr char HexDigits[] = "0123456789abcdef";
			std::string text;
			text.reserve(digest.m_Value.size() * 2);
			for (const std::byte byte : digest.m_Value)
			{
				const uint8_t value = std::to_integer<uint8_t>(byte);
				text.push_back(HexDigits[value >> 4]);
				text.push_back(HexDigits[value & 0x0f]);
			}
			return text;
		}

		class Win32LocalDerivedDataMaintenanceLockGuard final :
			public LocalDerivedDataMaintenanceLockGuardBase
		{
		public:
			explicit Win32LocalDerivedDataMaintenanceLockGuard(
				win32::NamedMutexGuard&& guard) noexcept : m_Guard(std::move(guard))
			{
			}

			[[nodiscard]] bool IsAcquired() const noexcept override
			{
				return m_Guard.IsAcquired();
			}
			[[nodiscard]] bool WasAbandoned() const noexcept override
			{
				return m_Guard.WasAbandoned();
			}

		private:
			win32::NamedMutexGuard m_Guard;
		};

		class Win32LocalDerivedDataMaintenanceLock final :
			public LocalDerivedDataMaintenanceLockBase
		{
		public:
			explicit Win32LocalDerivedDataMaintenanceLock(
				const LocalDerivedDataRootIdentity& identity) noexcept :
				m_Mutex(MakeWin32LocalDerivedDataMaintenanceMutexName(identity))
			{
			}

			[[nodiscard]] bool IsValid() const noexcept override
			{
				return m_Mutex.IsValid();
			}
			[[nodiscard]] std::unique_ptr<LocalDerivedDataMaintenanceLockGuardBase>
				Acquire() const noexcept override
			{
				return std::make_unique<Win32LocalDerivedDataMaintenanceLockGuard>(
					m_Mutex.Acquire());
			}

		private:
			win32::NamedMutex m_Mutex;
		};

		class Win32LocalDerivedDataPlatform final : public LocalDerivedDataPlatformBase
		{
		public:
			[[nodiscard]] LocalDerivedDataRootIdentity ResolveRootIdentity(
				const std::filesystem::path& rootDirectory) const noexcept override
			{
				if (rootDirectory.empty())
				{
					return {};
				}
				std::error_code errorCode;
				std::filesystem::path canonicalRoot =
					std::filesystem::weakly_canonical(rootDirectory, errorCode);
				if (errorCode)
				{
					errorCode.clear();
					canonicalRoot = std::filesystem::absolute(rootDirectory, errorCode);
				}
				if (errorCode || canonicalRoot.empty())
				{
					return {};
				}
				canonicalRoot = canonicalRoot.lexically_normal().make_preferred();

				std::wstring normalized = canonicalRoot.native();
				std::ranges::replace(normalized, L'/', L'\\');
				while (normalized.size() > canonicalRoot.root_path().native().size() &&
					(normalized.back() == L'\\' || normalized.back() == L'/'))
				{
					normalized.pop_back();
				}
				normalized = utils::ToInvariantLowercase(normalized);
				const std::string canonicalUtf8 = utils::ToString(normalized);
				if (canonicalUtf8.empty())
				{
					return {};
				}
				const Sha256Digest digest = ComputeSha256(
					std::as_bytes(std::span{ canonicalUtf8.data(), canonicalUtf8.size() }));
				if (!digest.IsValid())
				{
					return {};
				}
				return {
					.m_CanonicalRoot = std::move(canonicalRoot),
					.m_PlatformIdentity = DigestText(digest),
				};
			}

			[[nodiscard]] std::unique_ptr<LocalDerivedDataMaintenanceLockBase>
				CreateMaintenanceLock(
					const LocalDerivedDataRootIdentity& identity) const noexcept override
			{
				return identity.IsValid()
					? std::make_unique<Win32LocalDerivedDataMaintenanceLock>(identity)
					: nullptr;
			}

			[[nodiscard]] std::string CreateUniquePathToken() noexcept override
			{
				return std::format("{}.{}.{}.{}", win32::GetCurrentProcessId(),
					win32::GetTickCount64(), reinterpret_cast<uintptr_t>(this),
					m_UniqueSerial.fetch_add(1, std::memory_order_relaxed));
			}

		private:
			std::atomic_uint64_t m_UniqueSerial = 1;
		};
	}

	std::unique_ptr<LocalDerivedDataPlatformBase>
		CreateWin32LocalDerivedDataPlatform() noexcept
	{
		return std::make_unique<Win32LocalDerivedDataPlatform>();
	}

	std::unique_ptr<LocalDerivedDataPlatformBase>
		CreateDefaultLocalDerivedDataPlatform() noexcept
	{
		return CreateWin32LocalDerivedDataPlatform();
	}

	std::wstring MakeWin32LocalDerivedDataMaintenanceMutexName(
		const LocalDerivedDataRootIdentity& identity)
	{
		if (!identity.IsValid())
		{
			return {};
		}
		return L"Local\\gglab.ddc." +
			std::wstring(identity.m_PlatformIdentity.begin(), identity.m_PlatformIdentity.end());
	}
}
