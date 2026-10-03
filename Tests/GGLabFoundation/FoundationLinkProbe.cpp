#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "GGLabFoundation/Async/ProgressChannel.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Platform/PlatformDefines.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabFoundation/Platform/Win/Win32DiagnosticOutput.h"
#include "GGLabFoundation/Platform/Win/Win32NamedMutex.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"
#include "GGLabFoundation/String/StringUtils.h"
#include "GGLabFoundation/Task/TaskSystem.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
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
	static_assert(utils::SaturatingAdd(std::uint8_t{ 3 }, std::uint8_t{ 4 }) == 7);
	static_assert(utils::SaturatingAdd(std::numeric_limits<std::uint8_t>::max(),
		std::uint8_t{ 1 }) == std::numeric_limits<std::uint8_t>::max());
	static_assert(utils::SaturatingMultiply(std::uint32_t{ 6 }, std::uint32_t{ 7 }) == 42);
	static_assert(utils::SaturatingMultiply(std::uint64_t{ 0 },
		std::numeric_limits<std::uint64_t>::max()) == 0);
	static_assert(utils::SaturatingMultiply(std::numeric_limits<std::uint64_t>::max(),
		std::uint64_t{ 2 }) == std::numeric_limits<std::uint64_t>::max());
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

		struct PaddingBoundaryVector
		{
			std::size_t m_Size;
			std::string_view m_Digest;
		};
		constexpr std::array PaddingBoundaryVectors{
			PaddingBoundaryVector{ 55,
				"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
			PaddingBoundaryVector{ 56,
				"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
			PaddingBoundaryVector{ 63,
				"7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" },
			PaddingBoundaryVector{ 64,
				"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
			PaddingBoundaryVector{ 65,
				"635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" },
		};
		std::array<char, 65> paddingInput;
		paddingInput.fill('a');
		const std::span<const std::byte> paddingBytes =
			std::as_bytes(std::span{ paddingInput });
		for (const PaddingBoundaryVector& vector : PaddingBoundaryVectors)
		{
			const std::span<const std::byte> input = paddingBytes.first(vector.m_Size);
			Sha256Builder streamedBoundary;
			if (!MatchesHex(ComputeSha256(input), vector.m_Digest) ||
				!streamedBoundary.AddBytes(input.first(vector.m_Size - 1)) ||
				!streamedBoundary.AddBytes(input.last(1)) ||
				!MatchesHex(streamedBoundary.Finish(), vector.m_Digest))
			{
				return false;
			}
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

#if GGLAB_PLATFORM_WINDOWS
		// This is intentionally a very loose regression guard, not a benchmark.
		// BCrypt completes this payload in a few milliseconds on supported Windows
		// hardware; the unoptimized scalar implementation takes well over the limit.
		constexpr std::size_t GuardPayloadBytes = 16ull * 1024ull * 1024ull;
		const std::vector<std::byte> guardPayload(GuardPayloadBytes, std::byte{ 0x5a });
		const auto guardBegin = std::chrono::steady_clock::now();
		const Sha256Digest guardDigest = ComputeSha256(guardPayload);
		const auto guardElapsed = std::chrono::steady_clock::now() - guardBegin;
		if (!guardDigest.IsValid() || guardElapsed >= std::chrono::milliseconds(500))
		{
			return false;
		}
#endif

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
			utils::FindLeaf("Foundation") == "Foundation" && utils::FindLeaf("/").empty();
	}

	template <typename Predicate>
	[[nodiscard]] bool WaitUntil(Predicate&& predicate,
		std::chrono::milliseconds timeout = std::chrono::seconds(2)) noexcept
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!predicate())
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

	[[nodiscard]] bool RunProgressTests() noexcept
	{
		auto channel = std::make_shared<ProgressChannel>();
		ProgressReporter reporter(channel);
		reporter.Report(0.4f, "first", "detail", 2, 5);
		reporter.Report(0.2f, "second");
		reporter.Subrange(0.5f, 0.75f).Report(0.5f, "subrange", {}, 1, 2);

		ProgressSnapshot snapshot = channel->GetSnapshot();
		if (!snapshot.HasProgress() || snapshot.m_Fraction != 0.625f ||
			snapshot.m_Stage != "subrange" || snapshot.m_CompletedUnits != 1 ||
			snapshot.m_TotalUnits != 2 || snapshot.m_Revision != 3)
		{
			return false;
		}

		std::vector<std::thread> reporters;
		for (uint32_t index = 0; index < 8; ++index)
		{
			reporters.emplace_back([channel, index]
				{ channel->Report(static_cast<float>(index + 1) / 8.0f, "parallel"); });
		}
		for (std::thread& thread : reporters)
		{
			thread.join();
		}
		snapshot = channel->GetSnapshot();
		return snapshot.m_Fraction == 1.0f && snapshot.m_Revision == 11;
	}

	struct RecordingLifecycleState final
	{
		std::atomic_uint32_t m_Created = 0;
		std::atomic_uint32_t m_Destroyed = 0;
		std::atomic_bool m_DestroyedOnCreatingThread = true;
	};

	class RecordingWorkerContext final : public TaskWorkerContext
	{
	public:
		explicit RecordingWorkerContext(std::shared_ptr<RecordingLifecycleState> state) noexcept :
			m_State(std::move(state)), m_CreatingThread(std::this_thread::get_id())
		{
			++m_State->m_Created;
		}

		~RecordingWorkerContext() override
		{
			if (std::this_thread::get_id() != m_CreatingThread)
			{
				m_State->m_DestroyedOnCreatingThread = false;
			}
			++m_State->m_Destroyed;
		}

	private:
		std::shared_ptr<RecordingLifecycleState> m_State;
		std::thread::id m_CreatingThread;
	};

	class RecordingWorkerLifecycle final : public TaskWorkerLifecycle
	{
	public:
		explicit RecordingWorkerLifecycle(std::shared_ptr<RecordingLifecycleState> state) noexcept :
			m_State(std::move(state))
		{
		}

		[[nodiscard]] std::unique_ptr<TaskWorkerContext> CreateContext(
			uint32_t workerIndex) const override
		{
			GGLAB_UNUSED(workerIndex);
			return std::make_unique<RecordingWorkerContext>(m_State);
		}

	private:
		std::shared_ptr<RecordingLifecycleState> m_State;
	};

	[[nodiscard]] bool RunTaskFailureTests() noexcept
	{
		std::vector<TaskCompletionInfo> completions;
		const std::thread::id ownerThread = std::this_thread::get_id();
		bool ownerThreadCallbacks = true;
		TaskSystem taskSystem({ .m_WorkerCount = 1 });
		std::array<TaskWork, 4> work{
			[](std::stop_token) { return TaskResult::Failure("Explicit task failure."); },
			[](std::stop_token) -> TaskResult { throw std::runtime_error("Task exception."); },
			[](std::stop_token) -> TaskResult { throw 42; },
			[](std::stop_token) { return TaskResult::Success(); },
		};
		for (TaskWork& taskWork : work)
		{
			if (!taskSystem.Submit({ .m_Name = "Foundation/failure recovery" },
				std::move(taskWork), [&](const TaskCompletionInfo& info)
				{
					ownerThreadCallbacks &= std::this_thread::get_id() == ownerThread;
					completions.push_back(info);
				}))
			{
				return false;
			}
		}
		if (!WaitUntil([&] { return taskSystem.GetStatistics().m_PendingCompletionCount == 4; }) ||
			taskSystem.PumpCompletions() != 4 || completions.size() != 4)
		{
			return false;
		}
		const TaskSystemStatistics statistics = taskSystem.GetStatistics();
		return ownerThreadCallbacks && completions[0].m_Status == TaskStatus::Failed &&
			completions[0].m_Error == "Explicit task failure." &&
			completions[1].m_Status == TaskStatus::Failed &&
			completions[1].m_Error == "Task exception." &&
			completions[2].m_Status == TaskStatus::Failed &&
			completions[2].m_Error == "Unknown task exception." &&
			completions[3].m_Status == TaskStatus::Succeeded && completions[3].m_Error.empty() &&
			statistics.m_FailedCount == 3 && statistics.m_SucceededCount == 1 &&
			statistics.m_CompletionCallbackCount == 4 && statistics.m_ActiveTasks.empty();
	}

	[[nodiscard]] bool RunTaskRunningCancellationTests() noexcept
	{
		std::atomic_bool started = false;
		std::atomic_bool observedStop = false;
		TaskCompletionInfo completion;
		TaskSystem taskSystem({ .m_WorkerCount = 1 });
		const TaskHandle handle = taskSystem.Submit({ .m_Name = "Foundation/cancel running" },
			[&](std::stop_token stopToken)
			{
				started = true;
				while (!stopToken.stop_requested())
				{
					std::this_thread::yield();
				}
				observedStop = true;
				return TaskResult::Failure("Cancellation takes precedence over failure.");
			},
			[&](const TaskCompletionInfo& info) { completion = info; });
		if (!handle || !WaitUntil([&] { return started.load(); }) || !taskSystem.Cancel(handle) ||
			!WaitUntil([&] { return taskSystem.GetStatistics().m_PendingCompletionCount == 1; }) ||
			taskSystem.PumpCompletions() != 1)
		{
			return false;
		}
		const TaskSystemStatistics statistics = taskSystem.GetStatistics();
		return observedStop.load() && completion.m_Handle == handle &&
			completion.m_Status == TaskStatus::Cancelled && completion.m_Error.empty() &&
			statistics.m_CancelledCount == 1 && statistics.m_FailedCount == 0 &&
			statistics.m_RunningCount == 0 && statistics.m_AcceptingTasks &&
			!taskSystem.Cancel(handle);
	}

	[[nodiscard]] bool RunTaskCompletionBacklogTests() noexcept
	{
		constexpr uint32_t TaskCount = 256;
		std::atomic_uint32_t executed = 0;
		std::array<TaskHandle, TaskCount> handles{};
		std::array<bool, TaskCount> completed{};
		uint32_t callbacks = 0;
		bool validCallbacks = true;
		const std::thread::id ownerThread = std::this_thread::get_id();
		TaskSystem taskSystem({ .m_WorkerCount = 2 });
		for (uint32_t index = 0; index < TaskCount; ++index)
		{
			handles[index] = taskSystem.Submit({ .m_Name = "Foundation/completion backlog" },
				[&](std::stop_token)
				{
					++executed;
					return TaskResult::Success();
				},
				[&, index](const TaskCompletionInfo& info)
				{
					validCallbacks &= !completed[index] && info.m_Handle == handles[index] &&
						info.m_Status == TaskStatus::Succeeded &&
						std::this_thread::get_id() == ownerThread;
					completed[index] = true;
					++callbacks;
				});
			if (!handles[index])
			{
				return false;
			}
		}
		if (!WaitUntil([&]
			{ return taskSystem.GetStatistics().m_PendingCompletionCount == TaskCount; }) ||
			executed.load() != TaskCount || callbacks != 0 ||
			taskSystem.PumpCompletions({ .m_MaxCallbacks = 0 }) != 0 ||
			taskSystem.GetStatistics().m_PendingCompletionCount != TaskCount)
		{
			return false;
		}
		// A time budget is checked after dispatch, so even a zero budget permits one callback.
		if (taskSystem.PumpCompletions({ .m_MaxMilliseconds = 0.0 }) != 1 || callbacks != 1)
		{
			return false;
		}
		while (callbacks < TaskCount)
		{
			const uint32_t expected = (TaskCount - callbacks) < 7 ? TaskCount - callbacks : 7;
			const uint32_t before = callbacks;
			if (taskSystem.PumpCompletions({ .m_MaxCallbacks = 7 }) != expected ||
				callbacks != before + expected ||
				taskSystem.GetStatistics().m_PendingCompletionCount != TaskCount - callbacks)
			{
				return false;
			}
		}
		const TaskSystemStatistics statistics = taskSystem.GetStatistics();
		return validCallbacks && statistics.m_SubmittedCount == TaskCount &&
			statistics.m_SucceededCount == TaskCount && statistics.m_CompletionCallbackCount == TaskCount &&
			statistics.m_CompletionCallbackFailureCount == 0 && statistics.m_ActiveTasks.empty() &&
			taskSystem.PumpCompletions() == 0;
	}

	[[nodiscard]] bool RunTaskCompletionLifetimeTests() noexcept
	{
		auto owner = std::make_shared<uint32_t>(0);
		const std::weak_ptr<uint32_t> weakOwner = owner;
		std::atomic_bool started = false;
		uint32_t expiredOwnerCallbacks = 0;
		TaskSystem taskSystem({ .m_WorkerCount = 1 });
		const TaskHandle handle = taskSystem.Submit({ .m_Name = "Foundation/expired owner" },
			[state = owner, &started](std::stop_token stopToken)
			{
				*state = 1;
				started = true;
				while (!stopToken.stop_requested())
				{
					std::this_thread::yield();
				}
				return TaskResult::Success();
			},
			[weakOwner, &expiredOwnerCallbacks](const TaskCompletionInfo&)
			{
				if (const auto state = weakOwner.lock())
				{
					++*state;
				}
				else
				{
					++expiredOwnerCallbacks;
				}
			});
		if (!handle || !WaitUntil([&] { return started.load(); }))
		{
			return false;
		}
		owner.reset();
		// Work retains its backing state until it finishes; a weak callback must not retain it.
		if (weakOwner.expired() || !taskSystem.Cancel(handle) || !WaitUntil([&]
			{ return taskSystem.GetStatistics().m_PendingCompletionCount == 1; }) ||
			!weakOwner.expired())
		{
			return false;
		}
		auto nextOwner = std::make_shared<uint32_t>(0);
		if (!taskSystem.Submit({ .m_Name = "Foundation/replacement owner" },
			[](std::stop_token) { return TaskResult::Success(); },
			[state = nextOwner](const TaskCompletionInfo&) { ++*state; }) ||
			!WaitUntil([&] { return taskSystem.GetStatistics().m_PendingCompletionCount == 2; }) ||
			taskSystem.PumpCompletions() != 2 || expiredOwnerCallbacks != 1 || *nextOwner != 1)
		{
			return false;
		}

		bool discardedCallbackRan = false;
		std::weak_ptr<uint32_t> pendingOwner;
		{
			TaskSystem pendingSystem({ .m_WorkerCount = 1 });
			auto state = std::make_shared<uint32_t>(0);
			pendingOwner = state;
			if (!pendingSystem.Submit({ .m_Name = "Foundation/discard pending completion" },
				[](std::stop_token) { return TaskResult::Success(); },
				[state, &discardedCallbackRan](const TaskCompletionInfo&)
				{
					++*state;
					discardedCallbackRan = true;
				}) || !WaitUntil([&]
				{ return pendingSystem.GetStatistics().m_PendingCompletionCount == 1; }))
			{
				return false;
			}
			state.reset();
			if (pendingOwner.expired())
			{
				return false;
			}
		}
		return pendingOwner.expired() && !discardedCallbackRan;
	}

	[[nodiscard]] bool RunTaskTests() noexcept
	{
		const std::thread::id ownerThread = std::this_thread::get_id();
		std::thread::id workThread;
		std::thread::id completionThread;
		TaskStatus successStatus = TaskStatus::Invalid;
		{
			// No lifecycle policy exercises the portable, independently linkable default path.
			TaskSystem taskSystem({ .m_WorkerCount = 1 });
			const TaskHandle handle = taskSystem.Submit(
				{ .m_Name = "Foundation/default lifecycle" },
				[&](std::stop_token)
				{
					workThread = std::this_thread::get_id();
					return TaskResult::Success();
				},
				[&](const TaskCompletionInfo& info)
				{
					completionThread = std::this_thread::get_id();
					successStatus = info.m_Status;
				});
			if (!handle || !WaitUntil([&]
				{ return taskSystem.GetStatistics().m_PendingCompletionCount == 1; }) ||
				taskSystem.PumpCompletions() != 1 || successStatus != TaskStatus::Succeeded ||
				workThread == ownerThread || completionThread != ownerThread)
			{
				return false;
			}
		}

		const auto lifecycleState = std::make_shared<RecordingLifecycleState>();
		const auto lifecycle = std::make_shared<RecordingWorkerLifecycle>(lifecycleState);
		std::atomic_bool blockerStarted = false;
		std::atomic_bool releaseBlocker = false;
		std::atomic_bool backgroundExecuted = false;
		std::vector<uint32_t> executionOrder;
		std::mutex executionMutex;
		TaskStatus cancelledStatus = TaskStatus::Invalid;
		{
			TaskSystem taskSystem({
				.m_WorkerCount = 1,
				.m_WorkerLifecycle = lifecycle,
				});
			if (!WaitUntil([&] { return lifecycleState->m_Created.load() == 1; }))
			{
				return false;
			}

			const TaskHandle blocker = taskSystem.Submit({ .m_Name = "Foundation/blocker" },
				[&](std::stop_token stopToken)
				{
					blockerStarted = true;
					while (!releaseBlocker.load() && !stopToken.stop_requested())
					{
						std::this_thread::yield();
					}
					return TaskResult::Success();
				});
			if (!blocker || !WaitUntil([&] { return blockerStarted.load(); }))
			{
				return false;
			}

			const TaskHandle background = taskSystem.Submit(
				{ .m_Name = "Foundation/background", .m_Priority = TaskPriority::Background },
				[&](std::stop_token)
				{
					backgroundExecuted = true;
					return TaskResult::Success();
				},
				[&](const TaskCompletionInfo& info) { cancelledStatus = info.m_Status; });
			constexpr std::array Priorities{
				TaskPriority::Background, TaskPriority::High, TaskPriority::Normal, TaskPriority::Critical,
			};
			// Queue every priority twice behind one running worker to verify priority and FIFO order.
			for (uint32_t index = 0; index < 8; ++index)
			{
				if (!taskSystem.Submit(
					{ .m_Name = "Foundation/priority order", .m_Priority = Priorities[index % Priorities.size()] },
					[&, index](std::stop_token)
					{
						std::scoped_lock lock(executionMutex);
						executionOrder.push_back(index);
						return TaskResult::Success();
					}))
				{
					return false;
				}
			}
			if (!background || !taskSystem.Cancel(background))
			{
				return false;
			}
			releaseBlocker = true;
			if (!WaitUntil([&]
				{ return taskSystem.GetStatistics().m_PendingCompletionCount == 10; }))
			{
				return false;
			}
			if (taskSystem.PumpCompletions() != 10)
			{
				return false;
			}
			taskSystem.Shutdown();
		}
		if (lifecycleState->m_Destroyed.load() != 1 ||
			!lifecycleState->m_DestroyedOnCreatingThread.load() || backgroundExecuted.load() ||
			cancelledStatus != TaskStatus::Cancelled ||
			executionOrder != std::vector<uint32_t>{ 3, 7, 1, 5, 2, 6, 0, 4 })
		{
			return false;
		}

		std::atomic_bool shutdownTaskStarted = false;
		std::atomic_bool shutdownQueuedExecuted = false;
		TaskStatus shutdownStatus = TaskStatus::Invalid;
		TaskStatus shutdownQueuedStatus = TaskStatus::Invalid;
		TaskSystem shutdownSystem({ .m_WorkerCount = 1 });
		const TaskHandle shutdownTask = shutdownSystem.Submit(
			{ .m_Name = "Foundation/shutdown" },
			[&](std::stop_token stopToken)
			{
				shutdownTaskStarted = true;
				while (!stopToken.stop_requested())
				{
					std::this_thread::yield();
				}
				return TaskResult::Success();
			},
			[&](const TaskCompletionInfo& info) { shutdownStatus = info.m_Status; });
		if (!shutdownTask || !WaitUntil([&] { return shutdownTaskStarted.load(); }))
		{
			return false;
		}
		const TaskHandle shutdownQueued = shutdownSystem.Submit(
			{ .m_Name = "Foundation/shutdown queued" },
			[&](std::stop_token)
			{
				shutdownQueuedExecuted = true;
				return TaskResult::Success();
			},
			[&](const TaskCompletionInfo& info) { shutdownQueuedStatus = info.m_Status; });
		if (!shutdownQueued)
		{
			return false;
		}
		shutdownSystem.Shutdown();
		shutdownSystem.Shutdown();
		const TaskSystemStatistics shutdownStatistics = shutdownSystem.GetStatistics();
		if (shutdownSystem.PumpCompletions() != 2 || shutdownStatus != TaskStatus::Cancelled ||
			shutdownQueuedStatus != TaskStatus::Cancelled || shutdownQueuedExecuted.load() ||
			shutdownSystem.IsAcceptingTasks() || shutdownStatistics.m_WorkerCount != 0 ||
			shutdownStatistics.m_RunningCount != 0 || shutdownStatistics.m_CancelledCount != 2 ||
			shutdownSystem.Submit({ .m_Name = "Foundation/rejected after shutdown" },
				[](std::stop_token) { return TaskResult::Success(); }))
		{
			return false;
		}
		return RunTaskFailureTests() && RunTaskRunningCancellationTests() &&
			RunTaskCompletionBacklogTests() && RunTaskCompletionLifetimeTests();
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
		if (!contender.Acquire(0).IsAcquired())
		{
			return false;
		}

		TaskSystem windowsTaskSystem({
			.m_WorkerCount = 1,
			.m_WorkerLifecycle = std::make_shared<win32::Win32TaskWorkerLifecycle>(),
			});
		windowsTaskSystem.Shutdown();
		return true;
	}
}

int main()
{
	const bool linked = gglab::foundation::detail::FoundationLinkAnchor();
	return linked && gglab::foundation::tests::RunPrimitiveTests() &&
		gglab::foundation::tests::RunSha256Tests() &&
		gglab::foundation::tests::RunStringTests() &&
		gglab::foundation::tests::RunProgressTests() &&
		gglab::foundation::tests::RunTaskTests() &&
		gglab::foundation::tests::RunPathTests() &&
		gglab::foundation::tests::RunLoggingTests() &&
		gglab::foundation::tests::RunWindowsLeafTests()
		? 0
		: 1;
}
