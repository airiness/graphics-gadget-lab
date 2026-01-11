#pragma once
#include "CoreMacros.h"

#include <type_traits>
#include <concepts>
#include <utility>
#include <ranges>

namespace gglab::utils
{
	template<typename T>
	concept Integer = std::is_integral_v<std::remove_cvref_t<T>>;

	template<typename T>
	concept UnsignedInteger =
		std::is_unsigned_v<std::remove_cvref_t<T>> && Integer<T>;

	template<typename T>
	concept Enum = std::is_enum_v<std::remove_cvref_t<T>>;

	template<Enum E>
	[[nodiscard]] constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept
	{
		return static_cast<std::underlying_type_t<E>>(e);
	}

	// ADL hook. User provides overloads in the enum's associated namespace.
	void tag_invoke();

	namespace detail
	{
		template<typename Tag, typename... Args>
		concept TagInvocable = requires(const Tag & tag, Args&&... args)
		{
			tag_invoke(tag, std::forward<Args>(args)...);
		};

		template<typename Tag, typename... Args>
		using tag_invoke_result_t =
			decltype(tag_invoke(std::declval<Tag>(), std::declval<Args>()...));
	}

	struct ToIndexType
	{
		template<Enum E>
		[[nodiscard]] constexpr std::size_t operator()(E e) const noexcept
		{
			// user customize tag_invoke
			if constexpr (detail::TagInvocable<ToIndexType, E>)
			{
				using Result = detail::tag_invoke_result_t<ToIndexType, E>;
				static_assert(std::integral<Result>,
					"tag_invoke(ToIndexType, E) must return an integral type.");
				return static_cast<std::size_t>(tag_invoke(*this, e));
			}
			// underlying -> size_t
			else
			{
				using UT = std::underlying_type_t<E>;
				const UT value = std::to_underlying(e);

				if constexpr (std::is_signed_v<UT>)
				{
					GGLAB_ASSERT_MSG(value >= 0, "Enum value is negative; not valid as an array index.");
				}

				using UUT = std::make_unsigned_t<UT>;
				return static_cast<std::size_t>(static_cast<UUT>(value));
			}
		}
	};

	inline constexpr ToIndexType ToIndex{};

	template<Enum E>
	[[nodiscard]] constexpr std::size_t ToIndexChecked(E e, E countSentinel) noexcept
	{
		const std::size_t index = ToIndex(e);
		const std::size_t count = ToIndex(countSentinel);
		GGLAB_ASSERT_MSG(index < count, "Enum index out of range.");
		return index;
	}

	template<Enum E>
	[[nodiscard]] constexpr std::size_t ToIndexChecked(E e) noexcept
		requires requires { E::Count; }
	{
		return ToIndexChecked(e, E::Count);
	}

	template<Enum E>
	[[nodiscard]] constexpr std::size_t EnumCount() noexcept
		requires requires { E::Count; }
	{
		return ToIndex(E::Count);
	}

	template<Enum E>
	constexpr auto EnumRange() noexcept
	{
		return std::views::iota(std::size_t{ 0 }, EnumCount<E>())
			| std::views::transform([](std::size_t index)
				{
					using UT = std::underlying_type_t<E>;
					return static_cast<E>(static_cast<UT>(index));
				});
	}
}