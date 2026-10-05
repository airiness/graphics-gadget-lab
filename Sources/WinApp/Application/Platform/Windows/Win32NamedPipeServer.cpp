#include "Application/Platform/Windows/Win32NamedPipeServer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

namespace gglab::win32
{
	namespace
	{
		constexpr DWORD PipeBufferSize = 64 * 1024;
		constexpr DWORD RequestReadTimeoutMilliseconds = 10000;
		constexpr DWORD ResponseWriteTimeoutMilliseconds = 10000;
		constexpr size_t MaxRequestBytes = 1024 * 1024;
		constexpr DWORD PipeCreateRetryMilliseconds = 100;

		class ScopedHandle final
		{
		public:
			explicit ScopedHandle(HANDLE handle) noexcept : m_Handle(handle) {}
			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;
			~ScopedHandle() noexcept
			{
				if (m_Handle && m_Handle != INVALID_HANDLE_VALUE)
				{
					::CloseHandle(m_Handle);
				}
			}
			[[nodiscard]] HANDLE Get() const noexcept { return m_Handle; }

		private:
			HANDLE m_Handle = nullptr;
		};

		// Waits for an overlapped operation; cancels it on timeout or, when a stop
		// event is given, on stop.
		[[nodiscard]] bool CompleteOverlapped(HANDLE pipe, OVERLAPPED& overlapped,
			HANDLE stopEvent, DWORD timeoutMilliseconds, DWORD& outBytes) noexcept
		{
			const std::array handles{ overlapped.hEvent, stopEvent };
			const DWORD handleCount = stopEvent ? 2u : 1u;
			const DWORD wait = ::WaitForMultipleObjects(
				handleCount, handles.data(), FALSE, timeoutMilliseconds);
			if (wait != WAIT_OBJECT_0)
			{
				::CancelIoEx(pipe, &overlapped);
				::GetOverlappedResult(pipe, &overlapped, &outBytes, TRUE);
				return false;
			}
			return ::GetOverlappedResult(pipe, &overlapped, &outBytes, FALSE) != FALSE;
		}

		// DisconnectNamedPipe discards unread data, and a completed write only means
		// the response reached the pipe buffer. The client closes its end once it
		// read the response line, so waiting for that keeps the response intact.
		// Unlike FlushFileBuffers, the wait is bounded: a client that never reads
		// cannot hold the connection, and with it server shutdown.
		void WaitForClientClose(
			HANDLE pipe, HANDLE ioEvent, std::chrono::milliseconds timeout) noexcept
		{
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			std::array<char, 256> discarded{};
			while (true)
			{
				const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
					deadline - std::chrono::steady_clock::now());
				if (remaining.count() <= 0)
				{
					return;
				}
				OVERLAPPED overlapped{};
				overlapped.hEvent = ioEvent;
				::ResetEvent(ioEvent);
				DWORD bytes = 0;
				if (!::ReadFile(pipe, discarded.data(), static_cast<DWORD>(discarded.size()),
					nullptr, &overlapped) && ::GetLastError() != ERROR_IO_PENDING)
				{
					// ERROR_BROKEN_PIPE: the client closed its end.
					return;
				}
				// Bytes the client sends after its request line are ignored.
				if (!CompleteOverlapped(pipe, overlapped, nullptr,
					static_cast<DWORD>(remaining.count()), bytes) || bytes == 0)
				{
					return;
				}
			}
		}
	}

	void NamedPipeRequest::Respond(std::string response) noexcept
	{
		{
			std::scoped_lock lock(m_Mutex);
			if (m_Response)
			{
				return;
			}
			m_Response = std::move(response);
		}
		m_Condition.notify_all();
	}

	bool NamedPipeRequest::IsAnswered() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return m_Response.has_value();
	}

	std::optional<std::string> NamedPipeRequest::WaitForResponse(HANDLE stopEvent) noexcept
	{
		std::unique_lock lock(m_Mutex);
		while (!m_Response)
		{
			// Stop answers every outstanding request before signaling, so the
			// event only ends waits on requests created during shutdown.
			if (::WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
			{
				return std::nullopt;
			}
			m_Condition.wait_for(lock, std::chrono::milliseconds(50));
		}
		return m_Response;
	}

	NamedPipeServer::~NamedPipeServer()
	{
		Stop(R"({"ok":false,"error":"The session is shutting down."})");
	}

	bool NamedPipeServer::Start(
		std::wstring_view pipeName, std::chrono::milliseconds clientCloseTimeout) noexcept
	{
		if (m_StopEvent)
		{
			return false;
		}
		m_PipeName = pipeName;
		m_ClientCloseTimeout = clientCloseTimeout;
		m_StopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!m_StopEvent)
		{
			return false;
		}
		// The first instance claims the name, so a second session with the same
		// name fails here instead of sharing clients.
		m_FirstInstance = CreatePipeInstance(true);
		if (m_FirstInstance == INVALID_HANDLE_VALUE)
		{
			::CloseHandle(m_StopEvent);
			m_StopEvent = nullptr;
			return false;
		}
		m_Listener = std::thread([this]() noexcept { Listen(); });
		return true;
	}

	void NamedPipeServer::Stop(std::string_view fallbackResponse) noexcept
	{
		if (!m_StopEvent)
		{
			return;
		}
		{
			std::scoped_lock lock(m_Mutex);
			for (const std::shared_ptr<NamedPipeRequest>& request : m_Outstanding)
			{
				request->Respond(std::string(fallbackResponse));
			}
			m_Outstanding.clear();
			m_Pending.clear();
		}
		::SetEvent(m_StopEvent);
		if (m_Listener.joinable())
		{
			m_Listener.join();
		}
		std::list<Connection> connections;
		{
			std::scoped_lock lock(m_Mutex);
			connections.swap(m_Connections);
		}
		for (Connection& connection : connections)
		{
			if (connection.m_Thread.joinable())
			{
				connection.m_Thread.join();
			}
		}
		if (m_FirstInstance != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(m_FirstInstance);
			m_FirstInstance = INVALID_HANDLE_VALUE;
		}
		::CloseHandle(m_StopEvent);
		m_StopEvent = nullptr;
	}

	std::vector<std::shared_ptr<NamedPipeRequest>> NamedPipeServer::Poll() noexcept
	{
		std::vector<std::shared_ptr<NamedPipeRequest>> requests;
		std::scoped_lock lock(m_Mutex);
		requests.assign(m_Pending.begin(), m_Pending.end());
		m_Pending.clear();
		std::erase_if(m_Outstanding, [](const std::shared_ptr<NamedPipeRequest>& request)
			{ return request->IsAnswered(); });
		PruneFinishedConnections();
		return requests;
	}

	HANDLE NamedPipeServer::CreatePipeInstance(bool firstInstance) noexcept
	{
		const DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
			(firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0u);
		constexpr DWORD pipeMode =
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
		return ::CreateNamedPipeW(m_PipeName.c_str(), openMode, pipeMode,
			PIPE_UNLIMITED_INSTANCES, PipeBufferSize, PipeBufferSize, 0, nullptr);
	}

	void NamedPipeServer::PruneFinishedConnections() noexcept
	{
		for (auto connection = m_Connections.begin(); connection != m_Connections.end();)
		{
			if (connection->m_Done->load(std::memory_order_acquire))
			{
				connection->m_Thread.join();
				connection = m_Connections.erase(connection);
			}
			else
			{
				++connection;
			}
		}
	}

	void NamedPipeServer::Listen() noexcept
	{
		ScopedHandle connectEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
		HANDLE pipe = std::exchange(m_FirstInstance, INVALID_HANDLE_VALUE);
		while (::WaitForSingleObject(m_StopEvent, 0) != WAIT_OBJECT_0)
		{
			if (pipe == INVALID_HANDLE_VALUE)
			{
				pipe = CreatePipeInstance(false);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					::WaitForSingleObject(m_StopEvent, PipeCreateRetryMilliseconds);
					continue;
				}
			}

			OVERLAPPED overlapped{};
			overlapped.hEvent = connectEvent.Get();
			::ResetEvent(connectEvent.Get());
			bool connected = ::ConnectNamedPipe(pipe, &overlapped) != FALSE;
			const DWORD error = ::GetLastError();
			if (!connected && error == ERROR_PIPE_CONNECTED)
			{
				connected = true;
			}
			else if (!connected && error == ERROR_IO_PENDING)
			{
				DWORD bytes = 0;
				connected = CompleteOverlapped(pipe, overlapped, m_StopEvent, INFINITE, bytes);
			}
			if (!connected)
			{
				::CloseHandle(pipe);
				pipe = INVALID_HANDLE_VALUE;
				continue;
			}

			auto done = std::make_shared<std::atomic<bool>>(false);
			std::scoped_lock lock(m_Mutex);
			PruneFinishedConnections();
			m_Connections.push_back({
				.m_Thread = std::thread([this, pipe, done]() noexcept
					{
						Serve(pipe);
						done->store(true, std::memory_order_release);
					}),
				.m_Done = done,
				});
			pipe = INVALID_HANDLE_VALUE;
		}
		if (pipe != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(pipe);
		}
	}

	void NamedPipeServer::Serve(HANDLE pipe) noexcept
	{
		ScopedHandle pipeHandle(pipe);
		ScopedHandle ioEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
		std::string line;
		std::array<char, 4096> buffer{};
		bool complete = false;
		while (!complete && line.size() < MaxRequestBytes)
		{
			OVERLAPPED overlapped{};
			overlapped.hEvent = ioEvent.Get();
			::ResetEvent(ioEvent.Get());
			DWORD bytes = 0;
			if (!::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr,
				&overlapped))
			{
				if (::GetLastError() != ERROR_IO_PENDING)
				{
					break;
				}
			}
			if (!CompleteOverlapped(
				pipe, overlapped, m_StopEvent, RequestReadTimeoutMilliseconds, bytes) || bytes == 0)
			{
				break;
			}
			line.append(buffer.data(), bytes);
			const size_t newline = line.find('\n');
			if (newline != std::string::npos)
			{
				line.resize(newline);
				complete = true;
			}
		}
		if (!complete)
		{
			::DisconnectNamedPipe(pipe);
			return;
		}
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		auto request = std::make_shared<NamedPipeRequest>(std::move(line));
		{
			std::scoped_lock lock(m_Mutex);
			m_Pending.push_back(request);
			m_Outstanding.push_back(request);
		}
		std::optional<std::string> response = request->WaitForResponse(m_StopEvent);
		if (response)
		{
			response->push_back('\n');
			OVERLAPPED overlapped{};
			overlapped.hEvent = ioEvent.Get();
			::ResetEvent(ioEvent.Get());
			DWORD bytes = 0;
			const bool written = ::WriteFile(pipe, response->data(),
				static_cast<DWORD>(response->size()), nullptr, &overlapped) ||
				::GetLastError() == ERROR_IO_PENDING;
			// The stop event is not part of these waits: a response produced during
			// shutdown must still reach the client.
			if (written && CompleteOverlapped(
				pipe, overlapped, nullptr, ResponseWriteTimeoutMilliseconds, bytes))
			{
				WaitForClientClose(pipe, ioEvent.Get(), m_ClientCloseTimeout);
			}
		}
		::DisconnectNamedPipe(pipe);
	}
}
