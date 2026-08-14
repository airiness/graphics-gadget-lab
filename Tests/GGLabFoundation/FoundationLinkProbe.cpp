#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Base/TypeUtils.h"

#include <cstdint>
#include <type_traits>

namespace gglab::foundation::detail
{
	[[nodiscard]] bool FoundationLinkAnchor() noexcept;
}

namespace gglab::foundation::tests
{
	enum class TestFlags : std::uint8_t
	{
		None = 0,
		Read = 1 << 0,
		Write = 1 << 1,
	};
	GGLAB_ENUM_FLAGS(TestFlags)

	enum class TestValue : std::uint8_t
	{
		First,
		Second,
		Count,
	};

	struct TestIndexTag {};
	using TestIndex = TypedIndex<TestIndexTag, std::uint16_t>;

	struct MoveOnly
	{
		MoveOnly() = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(MoveOnly);
	};

	static_assert(!std::is_copy_constructible_v<MoveOnly>);
	static_assert(std::is_move_constructible_v<MoveOnly>);
	static_assert(Any(TestFlags::Read | TestFlags::Write));
	static_assert(Test(TestFlags::Read | TestFlags::Write, TestFlags::Write));
	static_assert(utils::ToUnderlying(TestValue::Second) == 1);
	static_assert(utils::ToIndex(TestValue::Second) == 1);
	static_assert(utils::EnumCount<TestValue>() == 2);
	static_assert(utils::AlignUp(std::uint32_t{ 5 }, std::uint32_t{ 4 }) == 8);
	static_assert(utils::AlignDown(std::uint32_t{ 7 }, std::uint32_t{ 4 }) == 4);
	static_assert(utils::IsPow2(std::uint32_t{ 8 }));
	static_assert(utils::AlignUpPow2(std::uint32_t{ 9 }, std::uint32_t{ 8 }) == 16);
	static_assert(TestIndex{ 7 }.Value() == 7);

	[[nodiscard]] bool RunPrimitiveTests() noexcept
	{
		IndexCounter<TestIndex> counter;
		const TestIndex first = counter.Acquire();
		const TestIndex second = counter.Acquire();
		return first.Value() == 0 && second.Value() == 1 && counter.Next() == 2;
	}
}

int main()
{
	const bool linked = gglab::foundation::detail::FoundationLinkAnchor();
	return linked && gglab::foundation::tests::RunPrimitiveTests() ? 0 : 1;
}
