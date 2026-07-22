#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataMaintenanceLock.h"
#include "Core/Hash/Sha256.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::wstring InvariantLowercase(std::wstring_view value) noexcept
		{
			if (value.empty()) return {};
			const int requiredCharacters = ::LCMapStringEx(
				LOCALE_NAME_INVARIANT,
				LCMAP_LOWERCASE,
				value.data(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr,
				0);
			if (requiredCharacters <= 0) return {};
			std::wstring result(static_cast<size_t>(requiredCharacters), L'\0');
			if (::LCMapStringEx(
				LOCALE_NAME_INVARIANT,
				LCMAP_LOWERCASE,
				value.data(),
				static_cast<int>(value.size()),
				result.data(),
				requiredCharacters,
				nullptr,
				nullptr,
				0) == 0)
			{
				return {};
			}
			return result;
		}

		[[nodiscard]] std::string WideToUtf8(std::wstring_view value) noexcept
		{
			if (value.empty()) return {};
			const int requiredBytes = ::WideCharToMultiByte(
				CP_UTF8,
				WC_ERR_INVALID_CHARS,
				value.data(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (requiredBytes <= 0) return {};
			std::string result(static_cast<size_t>(requiredBytes), '\0');
			if (::WideCharToMultiByte(
				CP_UTF8,
				WC_ERR_INVALID_CHARS,
				value.data(),
				static_cast<int>(value.size()),
				result.data(),
				requiredBytes,
				nullptr,
				nullptr) == 0)
			{
				return {};
			}
			return result;
		}

		[[nodiscard]] std::wstring DigestText(const Sha256Hash& digest)
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

	LocalDerivedDataRootIdentity ResolveLocalDerivedDataRootIdentity(
		const std::filesystem::path& rootDirectory) noexcept
	{
		if (rootDirectory.empty()) return {};
		std::error_code errorCode;
		std::filesystem::path canonicalRoot =
			std::filesystem::weakly_canonical(rootDirectory, errorCode);
		if (errorCode)
		{
			errorCode.clear();
			canonicalRoot = std::filesystem::absolute(rootDirectory, errorCode);
		}
		if (errorCode || canonicalRoot.empty()) return {};
		canonicalRoot = canonicalRoot.lexically_normal().make_preferred();

		std::wstring normalized = canonicalRoot.native();
		std::ranges::replace(normalized, L'/', L'\\');
		while (normalized.size() > canonicalRoot.root_path().native().size() &&
			(normalized.back() == L'\\' || normalized.back() == L'/'))
		{
			normalized.pop_back();
		}
		normalized = InvariantLowercase(normalized);
		const std::string canonicalUtf8 = WideToUtf8(normalized);
		if (canonicalUtf8.empty()) return {};
		const Sha256Hash digest = ComputeSha256(
			std::as_bytes(std::span{ canonicalUtf8.data(), canonicalUtf8.size() }));
		if (!digest.IsValid()) return {};
		return {
			.m_CanonicalRoot = std::move(canonicalRoot),
			.m_CanonicalUtf8 = canonicalUtf8,
			.m_MutexName = L"Local\\gglab.ddc." + DigestText(digest),
		};
	}

	LocalDerivedDataMaintenanceLock::LocalDerivedDataMaintenanceLock(
		const LocalDerivedDataRootIdentity& identity) noexcept :
		m_Mutex(identity.m_MutexName)
	{
	}
}
