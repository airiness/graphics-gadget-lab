#pragma once
#include "Core/Utility/TypeUtils.h"

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gglab::devtools
{
	template<utils::Enum E>
	struct EnumTextEntry
	{
		E m_Value{};
		std::string_view m_Text{};
	};

	template<utils::Enum E>
	struct EnumTextTraits;

	namespace detail
	{
		template<typename E>
		concept HasEnumTextEntries = requires
		{
			EnumTextTraits<E>::Entries;
		};

		template<typename E>
		concept HasUnknownText = requires
		{
			EnumTextTraits<E>::UnknownText;
		};

		template<typename E>
		concept HasNoneText = requires
		{
			EnumTextTraits<E>::NoneText;
		};

		template<typename E>
		concept HasSeparatorText = requires
		{
			EnumTextTraits<E>::SeparatorText;
		};

		template<typename E>
		concept HasAliasSeparatorText = requires
		{
			EnumTextTraits<E>::AliasSeparatorText;
		};

		template<utils::Enum E>
		[[nodiscard]] constexpr std::string_view UnknownText() noexcept
		{
			if constexpr (HasUnknownText<E>)
			{
				return EnumTextTraits<E>::UnknownText;
			}
			else
			{
				return "Unknown";
			}
		}

		template<utils::Enum E>
		[[nodiscard]] constexpr std::string_view NoneText() noexcept
		{
			if constexpr (HasNoneText<E>)
			{
				return EnumTextTraits<E>::NoneText;
			}
			else
			{
				return "None";
			}
		}

		template<utils::Enum E>
		[[nodiscard]] constexpr std::string_view SeparatorText() noexcept
		{
			if constexpr (HasSeparatorText<E>)
			{
				return EnumTextTraits<E>::SeparatorText;
			}
			else
			{
				return " | ";
			}
		}

		template<utils::Enum E>
		[[nodiscard]] constexpr std::string_view AliasSeparatorText() noexcept
		{
			if constexpr (HasAliasSeparatorText<E>)
			{
				return EnumTextTraits<E>::AliasSeparatorText;
			}
			else
			{
				return "/";
			}
		}
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumText(E value)
		requires detail::HasEnumTextEntries<E>
	{
		std::string result;
		const std::string_view separator = detail::AliasSeparatorText<E>();
		for (const auto& entry : EnumTextTraits<E>::Entries)
		{
			if (entry.m_Value == value)
			{
				if (!result.empty())
				{
					result.append(separator);
				}
				result.append(entry.m_Text);
			}
		}

		return result.empty() ? std::string(detail::UnknownText<E>()) : result;
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumValueText(E value)
		requires detail::HasEnumTextEntries<E>
	{
		const std::string text = EnumText(value);
		if (std::string_view(text.data(), text.size()) != detail::UnknownText<E>())
		{
			return text;
		}

		return std::format("{}({})", text, static_cast<uint64_t>(utils::ToUnderlying(value)));
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumValueTextWithBits(E value)
		requires detail::HasEnumTextEntries<E>
	{
		return std::format("{}(0x{:X})",
			EnumText(value),
			static_cast<uint64_t>(utils::ToUnderlying(value)));
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumFlagsText(std::underlying_type_t<E> bits)
		requires detail::HasEnumTextEntries<E>
	{
		if (bits == 0)
		{
			return std::string(detail::NoneText<E>());
		}

		std::string result;
		auto remainingBits = bits;
		const std::string_view separator = detail::SeparatorText<E>();
		for (const auto& entry : EnumTextTraits<E>::Entries)
		{
			const auto flag = utils::ToUnderlying(entry.m_Value);
			if (flag == 0 || (remainingBits & flag) != flag)
			{
				continue;
			}

			if (!result.empty())
			{
				result.append(separator);
			}
			result.append(EnumText(entry.m_Value));
			remainingBits = static_cast<std::underlying_type_t<E>>(remainingBits & ~flag);
		}

		if (remainingBits != 0)
		{
			if (!result.empty())
			{
				result.append(separator);
			}
			result.append(std::format("UnknownBits(0x{:X})", static_cast<uint64_t>(remainingBits)));
		}

		return result.empty() ? std::string(detail::UnknownText<E>()) : result;
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumFlagsText(E flags)
		requires detail::HasEnumTextEntries<E>
	{
		return EnumFlagsText<E>(utils::ToUnderlying(flags));
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumFlagsTextWithBits(std::underlying_type_t<E> bits)
		requires detail::HasEnumTextEntries<E>
	{
		return std::format("{}(0x{:X})",
			EnumFlagsText<E>(bits),
			static_cast<uint64_t>(bits));
	}

	template<utils::Enum E>
	[[nodiscard]] std::string EnumFlagsTextWithBits(E flags)
		requires detail::HasEnumTextEntries<E>
	{
		return EnumFlagsTextWithBits<E>(utils::ToUnderlying(flags));
	}
}
