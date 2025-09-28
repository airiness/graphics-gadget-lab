#pragma once
#include <cstdint>
#include <limits>
#include <functional>
#include <compare>
#include <type_traits>

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

	template<typename IndexType>
	class IndexCounter
	{
	public:
		using Rep = typename IndexType::ValueType;
		static_assert(std::is_unsigned_v<Rep>, "Rep must be unsigned integer type.");

		constexpr IndexCounter(Rep start = 0) noexcept : m_Next((start == IndexType::InvalidValue) ? 0 : start) {}

		void Reset(Rep start = 0) noexcept
		{
			m_Next = (start == IndexType::InvalidValue) ? 0 : start;
		}

		[[nodiscard]] Rep Next() const noexcept { return m_Next; }

		[[nodiscard]] IndexType Acquire() noexcept
		{
			if (m_Next == IndexType::InvalidValue) { m_Next = 0; }
			return IndexType{ m_Next++ };
		}

	private:
		Rep m_Next = 0;
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

	// TypedIndex with counter define helper macro.
#define GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(name, repType)	\
    struct name##Tag {};										\
    using name = ::gglab::TypedIndex<name##Tag, repType>;		\
    inline constexpr name Invalid##name = name::Invalid();		\
    using name##Counter = ::gglab::IndexCounter<name>;

	// Nested TypedIndex with counter define helper macro.
#define GGLAB_DEFINE_NESTED_TYPED_INDEX_WITH_COUNTER(name, repType)	\
    struct name##Tag {};                                            \
    using name = ::gglab::TypedIndex<name##Tag, repType>;           \
    inline static constexpr name Invalid##name = name::Invalid();   \
    using name##Counter = ::gglab::IndexCounter<name>;

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