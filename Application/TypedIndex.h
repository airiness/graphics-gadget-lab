#pragma once
#include <cstdint>
#include <limits>
#include <functional>
#include <compare>

namespace gglab
{
	template<typename Tag, typename Rep = uint32_t>
	class TypedIndex
	{
	public:
		using ValueType = Rep;
		static constexpr ValueType InvalidValue = std::numeric_limits<ValueType>::max();

		constexpr TypedIndex() = default;
		explicit constexpr TypedIndex(ValueType value) : m_Value(value) {}

		constexpr TypedIndex& operator=(ValueType v) noexcept { m_Value = v; return *this; }
		constexpr void Reset() noexcept { m_Value = InvalidValue; }

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != InvalidValue; }
		[[nodiscard]] constexpr ValueType Value() const noexcept { return m_Value; }
		[[nodiscard]] static constexpr TypedIndex Invalid() noexcept { return TypedIndex{ InvalidValue }; }

		friend constexpr auto operator<=>(const TypedIndex&, const TypedIndex&) noexcept = default;

		explicit constexpr operator ValueType() const noexcept { return m_Value; }

	private:
		ValueType m_Value = InvalidValue;
	};

	// TypedIndex define helper macro.
#define GGLAB_DEFINE_TYPED_INDEX(name, repType)				\
	struct name##Tag {};									\
	using name = ::gglab::TypedIndex<name##Tag, repType>;	\
	inline constexpr name Invalid##name = name::Invalid();

	// Nested TypedIndex define helper macro.
#define GGLAB_DEFINE_NESTED_TYPED_INDEX(name, repType)		\
	struct name##Tag {};									\
	using name = ::gglab::TypedIndex<name##Tag, repType>;	\
	inline static constexpr name Invalid##name = name::Invalid();
}

namespace std
{
	template<typename Tag, typename Rep>
	struct hash<gglab::TypedIndex<Tag, Rep>>
	{
		size_t operator()(gglab::TypedIndex<Tag, Rep> id) const noexcept
		{
			return std::hash<Rep>{}(id.Value());
		}
	};
}