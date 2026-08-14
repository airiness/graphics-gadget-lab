#include "Graphics/Asset/DerivedData/LocalDerivedDataMaintenanceLock.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "Core/Platform/Win/Win32NamedMutex.h"
#include "Core/Platform/Win/Win32StringUtils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::wstring DigestText(const Sha256Digest& digest)
		{
			constexpr wchar_t HexDigits[] = L"0123456789abcdef";
			std::wstring text;
			text.reserve(digest.m_Value.size() * 2);
			for (const std::byte byte : digest.m_Value)
			{
				const uint8_t value = std::to_integer<uint8_t>(byte);
				text.push_back(HexDigits[value >> 4]);
				text.push_back(HexDigits[value & 0x0f]);
			}
			return text;
		}
	}

	struct LocalDerivedDataMaintenanceLockGuard::Impl
	{
		explicit Impl(win32::NamedMutexGuard&& guard) : m_Guard(std::move(guard))
		{
		}

		win32::NamedMutexGuard m_Guard;
	};

	struct LocalDerivedDataMaintenanceLock::Impl
	{
		explicit Impl(std::wstring_view mutexName) : m_Mutex(mutexName)
		{
		}

		win32::NamedMutex m_Mutex;
	};

	LocalDerivedDataRootIdentity ResolveLocalDerivedDataRootIdentity(
		const std::filesystem::path& rootDirectory) noexcept
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
		const Sha256Digest digest =
			ComputeSha256(std::as_bytes(std::span{ canonicalUtf8.data(), canonicalUtf8.size() }));
		if (!digest.IsValid())
		{
			return {};
		}
		return {
			.m_CanonicalRoot = std::move(canonicalRoot),
			.m_CanonicalUtf8 = canonicalUtf8,
			.m_MutexName = L"Local\\gglab.ddc." + DigestText(digest),
		};
	}

	LocalDerivedDataMaintenanceLockGuard::LocalDerivedDataMaintenanceLockGuard() noexcept = default;

	LocalDerivedDataMaintenanceLockGuard::LocalDerivedDataMaintenanceLockGuard(
		LocalDerivedDataMaintenanceLockGuard&& other) noexcept = default;

	LocalDerivedDataMaintenanceLockGuard& LocalDerivedDataMaintenanceLockGuard::operator=(
		LocalDerivedDataMaintenanceLockGuard&& other) noexcept = default;

	LocalDerivedDataMaintenanceLockGuard::~LocalDerivedDataMaintenanceLockGuard() = default;

	bool LocalDerivedDataMaintenanceLockGuard::IsAcquired() const noexcept
	{
		return m_Impl != nullptr && m_Impl->m_Guard.IsAcquired();
	}

	bool LocalDerivedDataMaintenanceLockGuard::WasAbandoned() const noexcept
	{
		return m_Impl != nullptr && m_Impl->m_Guard.WasAbandoned();
	}

	LocalDerivedDataMaintenanceLock::LocalDerivedDataMaintenanceLock(
		const LocalDerivedDataRootIdentity& identity) noexcept :
		m_Impl(identity.IsValid() ? std::make_unique<Impl>(identity.m_MutexName) : nullptr)
	{
	}

	LocalDerivedDataMaintenanceLock::~LocalDerivedDataMaintenanceLock() = default;

	bool LocalDerivedDataMaintenanceLock::IsValid() const noexcept
	{
		return m_Impl != nullptr && m_Impl->m_Mutex.IsValid();
	}

	LocalDerivedDataMaintenanceLockGuard LocalDerivedDataMaintenanceLock::Acquire() const noexcept
	{
		LocalDerivedDataMaintenanceLockGuard result;
		if (m_Impl != nullptr && m_Impl->m_Mutex.IsValid())
		{
			result.m_Impl = std::make_unique<LocalDerivedDataMaintenanceLockGuard::Impl>(
				m_Impl->m_Mutex.Acquire());
		}
		return result;
	}
}
