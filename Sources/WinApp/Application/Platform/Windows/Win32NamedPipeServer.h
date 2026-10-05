#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::win32
{
	// One request line read from a pipe client. The owner answers it exactly once
	// with Respond, from any thread; the connection then writes the response
	// line and closes.
	class NamedPipeRequest final
	{
	public:
		explicit NamedPipeRequest(std::string line) noexcept : m_Line(std::move(line)) {}

		[[nodiscard]] const std::string& GetLine() const noexcept { return m_Line; }
		void Respond(std::string response) noexcept;
		[[nodiscard]] bool IsAnswered() noexcept;
		// Waits until Respond was called or the stop event is signaled.
		[[nodiscard]] std::optional<std::string> WaitForResponse(HANDLE stopEvent) noexcept;

	private:
		std::string m_Line;
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::optional<std::string> m_Response;
	};

	// Local, line-oriented named pipe server. Each client connection sends one
	// UTF-8 request line and receives one response line. Remote clients are
	// rejected and the pipe name must not already be in use. Connections are
	// served on their own threads; requests reach the owner thread through Poll.
	class NamedPipeServer final
	{
	public:
		NamedPipeServer() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(NamedPipeServer);
		~NamedPipeServer();

		// pipeName is the full path, for example \\.\pipe\name.
		[[nodiscard]] bool Start(std::wstring_view pipeName) noexcept;
		// Requests that are not answered by then receive the fallback response.
		void Stop(std::string_view fallbackResponse) noexcept;
		[[nodiscard]] std::vector<std::shared_ptr<NamedPipeRequest>> Poll() noexcept;

	private:
		struct Connection
		{
			std::thread m_Thread;
			std::shared_ptr<std::atomic<bool>> m_Done;
		};

		void Listen() noexcept;
		void Serve(HANDLE pipe) noexcept;
		[[nodiscard]] HANDLE CreatePipeInstance(bool firstInstance) noexcept;
		void PruneFinishedConnections() noexcept;

		std::wstring m_PipeName;
		HANDLE m_StopEvent = nullptr;
		HANDLE m_FirstInstance = INVALID_HANDLE_VALUE;
		std::thread m_Listener;
		std::mutex m_Mutex;
		std::deque<std::shared_ptr<NamedPipeRequest>> m_Pending;
		std::vector<std::shared_ptr<NamedPipeRequest>> m_Outstanding;
		std::list<Connection> m_Connections;
	};
}
