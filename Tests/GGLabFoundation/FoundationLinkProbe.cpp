#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
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

	[[nodiscard]] bool MatchesHex(
		const Sha256Digest& digest, std::string_view expected) noexcept
	{
		return Sha256DigestToHex(digest) == expected;
	}

	[[nodiscard]] bool RunSha256Tests() noexcept
	{
		const Sha256Digest empty = ComputeSha256({});
		if (!MatchesHex(empty,
			"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))
		{
			return false;
		}

		constexpr std::string_view Abc = "abc";
		const Sha256Digest abc = ComputeSha256(std::as_bytes(std::span{ Abc }));
		if (!MatchesHex(abc,
			"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
		{
			return false;
		}

		constexpr std::string_view MultiBlock =
			"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
		const Sha256Digest multiBlock =
			ComputeSha256(std::as_bytes(std::span{ MultiBlock }));
		if (!MatchesHex(multiBlock,
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"))
		{
			return false;
		}

		constexpr std::string_view LongMultiBlock =
			"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
			"hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
		if (!MatchesHex(ComputeSha256(std::as_bytes(std::span{ LongMultiBlock })),
			"cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"))
		{
			return false;
		}

		constexpr std::string_view Chunked = "The quick brown fox jumps over the lazy dog";
		const std::span<const std::byte> chunkedBytes = std::as_bytes(std::span{ Chunked });
		Sha256Builder incremental;
		if (!incremental.IsValid() || !incremental.AddBytes(chunkedBytes.first(1)) ||
			!incremental.AddBytes(chunkedBytes.subspan(1, 7)) ||
			!incremental.AddBytes(chunkedBytes.subspan(8, 17)) ||
			!incremental.AddBytes(chunkedBytes.subspan(25)))
		{
			return false;
		}
		const Sha256Digest chunked = incremental.Finish();
		if (chunked != ComputeSha256(chunkedBytes) ||
			!MatchesHex(chunked,
				"d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"))
		{
			return false;
		}

		Sha256Builder encoded;
		if (!encoded.AddU8(0x12u) || !encoded.AddU16LE(0x3456u) ||
			!encoded.AddU32LE(0x789abcdeu) || !encoded.AddU64LE(0x0123456789abcdefu) ||
			!encoded.AddStringUtf8("gglab"))
		{
			return false;
		}
		const Sha256Digest encodedDigest = encoded.Finish();
		const Sha256Digest encodedDigestCopy = encodedDigest;
		if (!MatchesHex(encodedDigest,
			"06f10901c097fbdd0cce46cb8de1672527144373f5652bff1b529d54f490e34f") ||
			Sha256DigestToHex(encodedDigest, 4) != "06f10901" ||
			encodedDigest != encodedDigestCopy ||
			Sha256DigestHash{}(encodedDigest) != Sha256DigestHash{}(encodedDigestCopy))
		{
			return false;
		}

		return !encoded.IsValid() && !encoded.AddBytes({}) &&
			!encoded.Finish().IsValid() && Sha256DigestToHex({}).empty();
	}
}

int main()
{
	const bool linked = gglab::foundation::detail::FoundationLinkAnchor();
	return linked && gglab::foundation::tests::RunPrimitiveTests() &&
		gglab::foundation::tests::RunSha256Tests()
		? 0
		: 1;
}
