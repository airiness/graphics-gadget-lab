#include "TaskSystemLifetimeTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabFoundation/Logging/Log.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <latch>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace gglab::foundation::tests
{
	namespace
	{
		template <typename Predicate>
		bool WaitUntil(Predicate ready) noexcept
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!ready())
			{
				if (std::chrono::steady_clock::now() >= deadline)
				{
					return false;
				}
				std::this_thread::yield();
			}
			return true;
		}

		struct ReleaseProbe
		{
			explicit ReleaseProbe(std::function<void()> released) : m_Released(std::move(released)) {}
			~ReleaseProbe() { m_Released(); }
			std::function<void()> m_Released;
		};

		bool RunCancellationCallback() noexcept
		{
			TaskSystem tasks({ .m_WorkerCount = 1 });
			std::latch registered(1);
			std::latch finish(1);
			TaskHandle running;
			TaskHandle queued;
			bool callbackOK = false;
			bool queuedRan = false;
			uint32_t completions = 0;
			running = tasks.Submit({}, [&](std::stop_token stop)
			{
				std::stop_callback callback(stop, [&]() noexcept
				{
					const auto snapshot = tasks.GetStatistics();
					callbackOK = snapshot.m_RunningCount == 1 && tasks.IsAcceptingTasks() &&
						!tasks.Cancel(running) && tasks.Cancel(queued) &&
						tasks.UpdatePriority(queued, TaskPriority::High);
					const auto submitted = tasks.Submit({}, [](std::stop_token)
					{
						return TaskResult::Success();
					});
					callbackOK = callbackOK && submitted.IsValid();
				});
				registered.count_down();
				finish.wait();
				return TaskResult::Failure("cancelled work error must be cleared");
			}, [&](const TaskCompletionInfo& info)
			{
				callbackOK = callbackOK && info.m_Status == TaskStatus::Cancelled && info.m_Error.empty();
				++completions;
			});
			registered.wait();
			queued = tasks.Submit({}, [&](std::stop_token)
			{
				queuedRan = true;
				return TaskResult::Success();
			}, [&](const TaskCompletionInfo& info)
			{
				callbackOK = callbackOK && info.m_Status == TaskStatus::Cancelled;
				++completions;
			});
			const bool accepted = tasks.Cancel(running);
			finish.count_down();
			const bool finished = WaitUntil([&]()
			{
				return tasks.GetStatistics().m_PendingCompletionCount == 3;
			});
			const auto snapshot = tasks.GetStatistics();
			return accepted && finished && tasks.PumpCompletions() == 3 && callbackOK &&
				!queuedRan && completions == 2 && snapshot.m_CancelledCount == 2 &&
				snapshot.m_SucceededCount == 1 && !tasks.Cancel(running);
		}

		bool RunShutdownCallback() noexcept
		{
			std::atomic_bool stoppingObserved = false;
			TaskSystem tasks({ .m_WorkerCount = 1 });
			std::latch registered(1);
			std::latch finish(1);
			TaskHandle queued;
			bool callbackOK = false;
			bool queuedRan = false;
			const auto running = tasks.Submit({}, [&](std::stop_token stop)
			{
				std::stop_callback callback(stop, [&]() noexcept
				{
					callbackOK = !tasks.IsAcceptingTasks() &&
						tasks.GetStatistics().m_WorkerCount == 1 &&
						!tasks.UpdatePriority(queued, TaskPriority::High) &&
						!tasks.UpdatePriority(queued, TaskPriority::Normal) && !tasks.Cancel(queued) &&
						!tasks.Submit({}, [](std::stop_token) { return TaskResult::Success(); });
					callbackOK = callbackOK && WaitUntil([&]() { return stoppingObserved.load(); });
					finish.count_down();
				});
				registered.count_down();
				finish.wait();
				return TaskResult::Success();
			});
			registered.wait();
			queued = tasks.Submit({}, [&](std::stop_token)
			{
				queuedRan = true;
				return TaskResult::Success();
			});
			std::atomic_bool queriesOK = true;
			std::latch observerStarted(1);
			std::jthread observer([&](std::stop_token stop)
			{
				observerStarted.count_down();
				while (!stop.stop_requested())
				{
					const auto snapshot = tasks.GetStatistics();
					if (!snapshot.m_AcceptingTasks && snapshot.m_WorkerCount == 1)
					{
						stoppingObserved = true;
					}
					if (snapshot.m_WorkerCount > 1 || snapshot.m_RunningCount > 1)
					{
						queriesOK = false;
					}
				}
			});
			observerStarted.wait();
			tasks.Shutdown();
			tasks.Shutdown();
			observer.request_stop();
			observer.join();
			const auto snapshot = tasks.GetStatistics();
			return running && queued && callbackOK && !queuedRan && queriesOK && stoppingObserved &&
				snapshot.m_WorkerCount == 0 && snapshot.m_CancelledCount == 2 &&
				snapshot.m_PendingCompletionCount == 2 && !tasks.UpdatePriority(queued, TaskPriority::Normal) &&
				tasks.PumpCompletions() == 2;
		}

		bool RunFinishPublication(bool shutdown) noexcept
		{
			TaskSystem tasks({ .m_WorkerCount = 1 });
			std::latch workReturn(1);
			std::latch releasing(1);
			std::latch release(1);
			std::atomic_bool released = false;
			bool completionOK = false;
			auto capture = std::make_shared<ReleaseProbe>([&]()
			{
				releasing.count_down();
				release.wait();
				GGLAB_UNUSED(tasks.GetStatistics());
				released = true;
			});
			const auto handle = tasks.Submit({}, [capture, &workReturn](std::stop_token)
			{
				workReturn.wait();
				return TaskResult::Success();
			}, [&](const TaskCompletionInfo& info)
			{
				completionOK = released && info.m_Status == TaskStatus::Succeeded;
			});
			capture.reset();
			workReturn.count_down();
			releasing.wait();
			const auto interval = tasks.GetStatistics();
			const bool intervalOK = interval.m_SucceededCount == 1 && interval.m_RunningCount == 0 &&
				interval.m_ActiveTasks.empty() && interval.m_PendingCompletionCount == 0 &&
				!released && !tasks.Cancel(handle) && !tasks.UpdatePriority(handle, TaskPriority::High) &&
				tasks.PumpCompletions() == 0;
			if (shutdown)
			{
				std::atomic_bool joinObserved = false;
				std::jthread helper([&]()
				{
					if (WaitUntil([&]() { return !tasks.IsAcceptingTasks(); }))
					{
						joinObserved = tasks.GetStatistics().m_WorkerCount == 1 && !released;
					}
					release.count_down();
				});
				tasks.Shutdown();
				helper.join();
				if (!joinObserved || !released || tasks.GetStatistics().m_WorkerCount != 0)
				{
					return false;
				}
			}
			else
			{
				release.count_down();
			}
			const bool published = WaitUntil([&]()
			{
				return tasks.GetStatistics().m_PendingCompletionCount == 1;
			});
			return intervalOK && published && tasks.PumpCompletions() == 1 && completionOK &&
				tasks.PumpCompletions() == 0;
		}

		bool RunCaptureRelease(bool discard) noexcept
		{
			auto tasks = std::make_unique<TaskSystem>(TaskSystem::CreateInfo{ .m_WorkerCount = 1 });
			TaskSystem* const system = tasks.get();
			std::atomic_uint32_t releases = 0;
			std::atomic_bool releaseOK = true;
			bool callbackRan = false;
			const auto query = [&]()
				{
					GGLAB_UNUSED(system->GetStatistics());
					GGLAB_UNUSED(system->IsAcceptingTasks());
					GGLAB_UNUSED(system->Cancel({}));
					++releases;
				};
			auto workCapture = std::make_shared<ReleaseProbe>([&]()
			{
				query();
				const auto extra = system->Submit({}, [](std::stop_token) { return TaskResult::Success(); });
				releaseOK = extra.IsValid();
			});
			auto completionCapture = std::make_shared<ReleaseProbe>(query);
			auto progress = ProgressChannelPtr(new ProgressChannel(), [&](ProgressChannel* value)
			{
				query();
				delete value;
			});
			std::latch workReturn(1);
			const auto task = system->Submit({ .m_Progress = progress },
				[workCapture, &workReturn](std::stop_token)
				{
					workReturn.wait();
					return TaskResult::Success();
				},
				[completionCapture, &callbackRan](const TaskCompletionInfo&)
				{
					callbackRan = true;
				});
			workCapture.reset();
			completionCapture.reset();
			progress.reset();
			workReturn.count_down();
			if (!WaitUntil([&]() { return system->GetStatistics().m_PendingCompletionCount == 2; }))
			{
				return false;
			}
			if (!discard)
			{
				if (system->PumpCompletions() != 2)
				{
					return false;
				}
			}
			system->Shutdown();
			tasks.reset();
			return task && releaseOK && releases == 3 && callbackRan == !discard;
		}

		bool RunStopCallbackRaces() noexcept
		{
			TaskSystem tasks({ .m_WorkerCount = 1 });
			std::latch registered(1);
			std::latch callbackEntered(1);
			std::latch releaseCallback(1);
			std::latch destroyCallback(1);
			std::latch destroying(1);
			std::atomic_bool immediate = false;
			const auto handle = tasks.Submit({}, [&](std::stop_token stop)
			{
				auto callback = std::make_unique<std::stop_callback<std::function<void()>>>(stop, [&]()
				{
					GGLAB_UNUSED(tasks.GetStatistics());
					callbackEntered.count_down();
					releaseCallback.wait();
				});
				registered.count_down();
				destroyCallback.wait();
				destroying.count_down();
				callback.reset();
				std::stop_callback late(stop, [&]() noexcept
				{
					immediate = tasks.IsAcceptingTasks();
				});
				return TaskResult::Success();
			});
			registered.wait();
			bool accepted = false;
			std::jthread canceller([&]() { accepted = tasks.Cancel(handle); });
			callbackEntered.wait();
			destroyCallback.count_down();
			destroying.wait();
			const bool noPrematureCompletion = tasks.GetStatistics().m_PendingCompletionCount == 0 &&
				!tasks.Cancel(handle);
			releaseCallback.count_down();
			canceller.join();
			const bool finished = WaitUntil([&]()
			{
				return tasks.GetStatistics().m_PendingCompletionCount == 1;
			});
			return accepted && noPrematureCompletion && finished && immediate && tasks.PumpCompletions() == 1;
		}

		class ReentrantLogSink final : public LogSink
		{
		public:
			explicit ReentrantLogSink(TaskSystem& tasks) noexcept : m_Tasks(tasks) {}
			void Write(LogTag, LogLevel, std::string_view) noexcept override
			{
				m_Called = !m_Tasks.IsAcceptingTasks() && !m_Tasks.GetStatistics().m_AcceptingTasks;
			}
			TaskSystem& m_Tasks;
			bool m_Called = false;
		};

		bool RunRejectionLogging() noexcept
		{
			TaskSystem tasks({ .m_WorkerCount = 1 });
			tasks.Shutdown();
			const auto previous = GetLogSink();
			auto sink = std::make_shared<ReentrantLogSink>(tasks);
			SetLogSink(sink);
			const auto rejected = tasks.Submit({}, [](std::stop_token) { return TaskResult::Success(); });
			SetLogSink(previous);
			return !rejected && (sink->m_Called || !GGLAB_DEVELOPMENT_BUILD);
		}

		void RunViolation(std::string_view scenario) noexcept
		{
			auto tasks = std::make_unique<TaskSystem>(TaskSystem::CreateInfo{ .m_WorkerCount = 1 });
			const bool stopped = scenario.ends_with("-stopped");
			if (stopped)
			{
				tasks->Shutdown();
			}
			if (scenario.starts_with("wrong-thread"))
			{
				std::jthread thread([&]()
				{
					if (scenario.starts_with("wrong-thread-delete"))
					{
						tasks.reset();
					}
					else
					{
						tasks->Shutdown();
					}
				});
				return;
			}
			if (scenario == "shutdown-stop-reentry" || scenario == "pump-stop-reentry")
			{
				std::latch registered(1);
				std::latch callbackReturned(1);
				const auto task = tasks->Submit({}, [&](std::stop_token stop)
				{
					std::stop_callback callback(stop, [&]() noexcept
					{
						if (scenario == "pump-stop-reentry")
						{
							GGLAB_UNUSED(tasks->PumpCompletions());
						}
						else
						{
							tasks->Shutdown();
						}
						callbackReturned.count_down();
					});
					registered.count_down();
					// Keep registration alive until dispatch; observing stop_requested alone
					// could remove the callback before request_stop invokes it.
					callbackReturned.wait();
					return TaskResult::Success();
				});
				registered.wait();
				if (scenario == "pump-stop-reentry")
				{
					GGLAB_UNUSED(tasks->Cancel(task));
				}
				else
				{
					tasks->Shutdown();
				}
				return;
			}
			if (scenario == "shutdown-work-release")
			{
				std::latch workStarted(1);
				std::latch workReturn(1);
				auto capture = std::make_shared<ReleaseProbe>([&]() { tasks->Shutdown(); });
				GGLAB_UNUSED(tasks->Submit({}, [capture, &workStarted, &workReturn](std::stop_token)
				{
					workStarted.count_down();
					workReturn.wait();
					return TaskResult::Success();
				}));
				capture.reset();
				workStarted.wait();
				workReturn.count_down();
				tasks->Shutdown();
				return;
			}
			if (scenario == "shutdown-queued-release")
			{
				std::latch registered(1);
				std::latch finish(1);
				GGLAB_UNUSED(tasks->Submit({}, [&](std::stop_token stop)
				{
					std::stop_callback callback(stop, [&]() noexcept { finish.count_down(); });
					registered.count_down();
					finish.wait();
					return TaskResult::Success();
				}));
				registered.wait();
				auto capture = std::make_shared<ReleaseProbe>([&]() { tasks->Shutdown(); });
				GGLAB_UNUSED(tasks->Submit({}, [capture](std::stop_token)
				{
					return TaskResult::Success();
				}));
				capture.reset();
				tasks->Shutdown();
				return;
			}
			if (scenario == "shutdown-completion-release")
			{
				auto capture = std::make_shared<ReleaseProbe>([&]() { tasks->Shutdown(); });
				GGLAB_UNUSED(tasks->Submit({}, [](std::stop_token) { return TaskResult::Success(); },
					[capture](const TaskCompletionInfo&) {}));
				capture.reset();
				if (WaitUntil([&]() { return tasks->GetStatistics().m_PendingCompletionCount == 1; }))
				{
					GGLAB_UNUSED(tasks->PumpCompletions());
				}
				return;
			}
			// A queued completion remains available after explicit shutdown. Its callback
			// must never delete the scheduler, including in the otherwise idempotent state.
			if (stopped)
			{
				// Populate first in a fresh owner, then stop before pumping.
				tasks = std::make_unique<TaskSystem>(TaskSystem::CreateInfo{ .m_WorkerCount = 1 });
			}
			GGLAB_UNUSED(tasks->Submit({}, [](std::stop_token) { return TaskResult::Success(); },
				[&](const TaskCompletionInfo&)
				{
					if (scenario.starts_with("delete-callback"))
					{
						tasks.reset();
					}
					else
					{
						tasks->Shutdown();
					}
				}));
			if (!WaitUntil([&]() { return tasks->GetStatistics().m_PendingCompletionCount == 1; }))
			{
				return;
			}
			if (stopped)
			{
				tasks->Shutdown();
			}
			GGLAB_UNUSED(tasks->PumpCompletions());
		}

		struct ChildCase
		{
			const char* m_Name;
			const char* m_ExpectedViolation;
		};

		bool RunChild(const ChildCase& test) noexcept
		{
			std::array<wchar_t, 32768> executable{};
			if (!GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())))
			{
				return false;
			}
			std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --task-lifetime-child ";
			for (const char* character = test.m_Name; *character; ++character)
			{
				command += static_cast<wchar_t>(*character);
			}
			SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
			HANDLE read = nullptr;
			HANDLE write = nullptr;
			if (!CreatePipe(&read, &write, &security, 65536))
			{
				return false;
			}
			using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
			const Handle reader(read, &CloseHandle);
			Handle writer(write, &CloseHandle);
			if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0))
			{
				return false;
			}
			const HANDLE inputHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				&security, OPEN_EXISTING, 0, nullptr);
			if (inputHandle == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			const Handle input(inputHandle, &CloseHandle);
			STARTUPINFOW startup{ sizeof(startup) };
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdInput = input.get();
			startup.hStdOutput = write;
			startup.hStdError = write;
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
			{
				return false;
			}
			const Handle child(process.hProcess, &CloseHandle);
			const Handle thread(process.hThread, &CloseHandle);
			writer.reset();
			const DWORD wait = WaitForSingleObject(child.get(), 15000);
			if (wait != WAIT_OBJECT_0)
			{
				if (!TerminateProcess(child.get(), 125) ||
					WaitForSingleObject(child.get(), 5000) != WAIT_OBJECT_0)
				{
					// Never block reading a pipe whose writer could still be alive.
					std::fprintf(stderr, "Task lifetime %s: watchdog could not reap child\n", test.m_Name);
					return false;
				}
			}
			DWORD exitCode = 0;
			if (!GetExitCodeProcess(child.get(), &exitCode))
			{
				return false;
			}
			std::string output;
			std::array<char, 4096> buffer{};
			DWORD count = 0;
			while (ReadFile(read, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count)
			{
				output.append(buffer.data(), count);
			}
			const bool violation = output.find("TaskSystem contract violation:") != std::string::npos;
			const bool passed = wait == WAIT_OBJECT_0 &&
				(test.m_ExpectedViolation ?
					exitCode != 0 && violation && output.find(test.m_ExpectedViolation) != std::string::npos :
					exitCode == 0 && !violation);
			std::printf("Task lifetime %s: %s\n", test.m_Name, passed ? "PASS" : "FAIL");
			std::fflush(stdout);
			if (!passed)
			{
				std::fprintf(stderr, "Child exit=%lu, wait=%lu\n%s", exitCode, wait, output.c_str());
			}
			return passed;
		}
	}

	int RunTaskSystemLifetimeChild(std::string_view scenario) noexcept
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		if (scenario == "cancel-callback")
		{
			return RunCancellationCallback() ? 0 : 1;
		}
		if (scenario == "shutdown-callback")
		{
			return RunShutdownCallback() ? 0 : 1;
		}
		if (scenario == "finish-publication" || scenario == "shutdown-finishing")
		{
			return RunFinishPublication(scenario == "shutdown-finishing") ? 0 : 1;
		}
		if (scenario == "pump-release" || scenario == "discard-release")
		{
			return RunCaptureRelease(scenario == "discard-release") ? 0 : 1;
		}
		if (scenario == "stop-callback-races")
		{
			return RunStopCallbackRaces() ? 0 : 1;
		}
		if (scenario == "rejection-logging")
		{
			return RunRejectionLogging() ? 0 : 1;
		}
		RunViolation(scenario);
		return 2;
	}

	bool RunTaskSystemLifetimeTests() noexcept
	{
		constexpr std::array cases
		{
			ChildCase{ "cancel-callback", nullptr },
			ChildCase{ "shutdown-callback", nullptr },
			ChildCase{ "finish-publication", nullptr },
			ChildCase{ "shutdown-finishing", nullptr },
			ChildCase{ "pump-release", nullptr },
			ChildCase{ "discard-release", nullptr },
			ChildCase{ "stop-callback-races", nullptr },
			ChildCase{ "rejection-logging", nullptr },
			ChildCase{ "shutdown-stop-reentry", "shutdown reentered while stopping" },
			ChildCase{ "pump-stop-reentry", "recursive completion pumping" },
			ChildCase{ "shutdown-work-release", "shutdown must run on the owner thread" },
			ChildCase{ "shutdown-queued-release", "shutdown reentered while stopping" },
			ChildCase{ "shutdown-completion-release", "shutdown from an active operation or callback" },
			ChildCase{ "shutdown-completion", "shutdown from an active operation or callback" },
			ChildCase{ "delete-callback", "destruction during an active operation or callback" },
			ChildCase{ "delete-callback-stopped", "destruction during an active operation or callback" },
			ChildCase{ "wrong-thread-shutdown", "shutdown must run on the owner thread" },
			ChildCase{ "wrong-thread-shutdown-stopped", "shutdown must run on the owner thread" },
			ChildCase{ "wrong-thread-delete", "destruction must run on the owner thread" },
			ChildCase{ "wrong-thread-delete-stopped", "destruction must run on the owner thread" },
		};
		bool passed = true;
		for (const auto& test : cases)
		{
			passed = RunChild(test) && passed;
		}
		return passed;
	}
}
