#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabFoundation/Platform/Win/Win32DiagnosticOutput.h"
#include "GGLabFoundation/Platform/Win/Win32NamedMutex.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "GGLabFoundation/String/StringUtils.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

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
	GGLAB_ENUM_FLAGS(TestFlags);

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

	[[nodiscard]] bool RunStringTests() noexcept
	{
		constexpr std::array<std::uint8_t, 4> Bytes{ 0x00u, 0x12u, 0xabu, 0xffu };
		return utils::EqualsAsciiIgnoreCase("GGLab", "gglab") &&
			!utils::EqualsAsciiIgnoreCase("GGLab", "gglabs") &&
			utils::StartsWithAsciiIgnoreCase("GraphicsGadgetLab", "GRAPHICS") &&
			utils::ContainsAsciiIgnoreCase("GraphicsGadgetLab", "GADGET") &&
			utils::ContainsAsciiIgnoreCase("GraphicsGadgetLab", "") &&
			!utils::ContainsAsciiIgnoreCase("GraphicsGadgetLab", "runtime") &&
			utils::BytesToHexString(Bytes) == "0012abff" &&
			utils::FindLeaf("Foundation/Platform/Win/") == "Win" &&
			utils::FindLeaf("Foundation") == "Foundation" && utils::FindLeaf("/").empty() &&
			std::string_view(utils::BoolToString(true)) == "Yes" &&
			std::string_view(utils::BoolToString(false)) == "No";
	}

	class ScopedTestDirectory final
	{
	public:
		explicit ScopedTestDirectory(std::filesystem::path path) : m_Path(std::move(path)) {}
		GGLAB_DELETE_COPYABLE_MOVABLE(ScopedTestDirectory);
		~ScopedTestDirectory()
		{
			std::error_code errorCode;
			std::filesystem::remove_all(m_Path, errorCode);
		}

	private:
		std::filesystem::path m_Path;
	};

	[[nodiscard]] bool RunPathTests() noexcept
	{
		std::error_code errorCode;
		const std::filesystem::path temporaryRoot =
			std::filesystem::temp_directory_path(errorCode);
		if (errorCode || temporaryRoot.empty())
		{
			return false;
		}

		const std::filesystem::path testRoot = temporaryRoot /
			("gglab.foundation-path-test." + std::to_string(win32::GetCurrentProcessId()) + "." +
				std::to_string(win32::GetTickCount64()));
		ScopedTestDirectory cleanup(testRoot);
		constexpr std::array<std::byte, 4> Payload{
			std::byte{ 0x00 }, std::byte{ 0x12 }, std::byte{ 0xab }, std::byte{ 0xff }
		};
		const std::filesystem::path file = testRoot / "nested" / "fixture.BIN";
		if (!utils::WriteFileBinary(file, Payload) ||
			!utils::ExtensionEqualsAsciiIgnoreCase(file, ".bin") ||
			utils::LastWriteTimeTicks(file) == 0 || utils::Canonical(file).empty())
		{
			return false;
		}

		std::array<std::byte, Payload.size()> actual{};
		std::ifstream input(file, std::ios::binary);
		input.read(reinterpret_cast<char*>(actual.data()),
			static_cast<std::streamsize>(actual.size()));
		return input && actual == Payload &&
			utils::CreateDirectoryIfNotExist(file.parent_path()) &&
			utils::CreateParentDirectoryIfNotExist(testRoot / "other" / "file.bin");
	}

	struct CapturedLog final
	{
		std::string m_Tag;
		LogLevel m_Level = LogLevel::Info;
		std::string m_Message;
	};

	class CapturingLogSink final : public LogSink
	{
	public:
		void Write(LogTag tag, LogLevel level, std::string_view message) noexcept override
		{
			std::scoped_lock lock(m_Mutex);
			m_Records.push_back(CapturedLog{
				.m_Tag = std::string(tag.Name()),
				.m_Level = level,
				.m_Message = std::string(message),
			});
		}

		void Flush() noexcept override
		{
			std::scoped_lock lock(m_Mutex);
			m_WasFlushed = true;
		}

		[[nodiscard]] std::vector<CapturedLog> Records() const
		{
			std::scoped_lock lock(m_Mutex);
			return m_Records;
		}

		[[nodiscard]] bool WasFlushed() const noexcept
		{
			std::scoped_lock lock(m_Mutex);
			return m_WasFlushed;
		}

	private:
		mutable std::mutex m_Mutex;
		std::vector<CapturedLog> m_Records;
		bool m_WasFlushed = false;
	};

	[[nodiscard]] bool RunLoggingTests() noexcept
	{
		InitializeLogging();
		if (!IsLoggingInitialized())
		{
			return false;
		}

		const std::shared_ptr<LogSink> previousSink = GetLogSink();
		const auto capture = std::make_shared<CapturingLogSink>();
		SetLogSink(capture);
		constexpr LogTag TestTag{ "FOUNDATION_TEST" };
		Log(TestTag, LogLevel::Info, "formatted {}", 42);
		std::thread writer(
			[&]
			{
				Log(TestTag, LogLevel::Warning, "plain message");
			});
		writer.join();
		FlushLogs();
		SetLogSink(previousSink);

		const std::vector<CapturedLog> records = capture->Records();
		return capture->WasFlushed() && records.size() == 2 &&
			records[0].m_Tag == "FOUNDATION_TEST" && records[0].m_Level == LogLevel::Info &&
			records[0].m_Message == "formatted 42" &&
			records[1].m_Tag == "FOUNDATION_TEST" &&
			records[1].m_Level == LogLevel::Warning &&
			records[1].m_Message == "plain message";
	}

	[[nodiscard]] bool RunWindowsLeafTests() noexcept
	{
		constexpr std::string_view Utf8 = "Graphics Gadget Lab \xe5\x9f\xba\xe7\xa1\x80";
		const std::wstring wide = utils::ToWideString(Utf8);
		const std::string invalidUtf8(1, static_cast<char>(0xff));
		const std::wstring invalidUtf16(1, static_cast<wchar_t>(0xd800));
		if (wide.empty() || utils::ToString(wide) != Utf8 ||
			!utils::ToWideString(invalidUtf8).empty() ||
			!utils::ToString(invalidUtf16).empty() ||
			utils::ToInvariantLowercase(L"GGLab-123") != L"gglab-123" ||
			win32::GetCurrentProcessId() == 0 || win32::GetExecutableDirectory().empty() ||
			win32::FormatLocalTime().empty() ||
			!utils::StartsWithAsciiIgnoreCase(FormatHResult(E_FAIL), "0x80004005"))
		{
			return false;
		}

		Ensure(S_OK);
		ComPtr<IUnknown> emptyComPointer;
		if (emptyComPointer.Get() != nullptr || &win32::WriteDiagnosticOutput == nullptr)
		{
			return false;
		}

		const std::wstring mutexName = L"Local\\gglab.foundation-test." +
			std::to_wstring(win32::GetCurrentProcessId()) + L"." +
			std::to_wstring(win32::GetTickCount64());
		win32::NamedMutex owner(mutexName);
		win32::NamedMutex contender(mutexName);
		win32::NamedMutexGuard ownerGuard = owner.Acquire(0);
		if (!owner.IsValid() || !contender.IsValid() || !ownerGuard.IsAcquired())
		{
			return false;
		}

		std::atomic disposition{ win32::NamedMutexAcquireDisposition::Failed };
		std::thread thread(
			[&]
			{
				const win32::NamedMutexGuard guard = contender.Acquire(0);
				disposition.store(guard.GetDisposition(), std::memory_order_relaxed);
			});
		thread.join();
		if (disposition.load(std::memory_order_relaxed) !=
			win32::NamedMutexAcquireDisposition::TimedOut)
		{
			return false;
		}

		ownerGuard = {};
		return contender.Acquire(0).IsAcquired();
	}
}

int main()
{
	const bool linked = gglab::foundation::detail::FoundationLinkAnchor();
	return linked && gglab::foundation::tests::RunPrimitiveTests() &&
		gglab::foundation::tests::RunSha256Tests() &&
		gglab::foundation::tests::RunStringTests() &&
		gglab::foundation::tests::RunPathTests() &&
		gglab::foundation::tests::RunLoggingTests() &&
		gglab::foundation::tests::RunWindowsLeafTests()
		? 0
		: 1;
}
