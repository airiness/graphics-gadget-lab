#pragma once

namespace gglab
{
	struct CpuProfileSample
	{
		std::string m_Name;
		double m_InclusiveMilliseconds = 0.0;
		double m_SelfMilliseconds = 0.0;
		uint32_t m_CallCount = 0;
	};

	struct CpuProfileFrameSnapshot
	{
		uint64_t m_FrameIndex = 0;
		double m_FrameMilliseconds = 0.0;
		std::vector<CpuProfileSample> m_Samples;

		[[nodiscard]] bool IsValid() const noexcept { return m_FrameIndex != 0; }
	};

	// Lightweight frame-oriented CPU profiler. The latest completed frame is
	// published as an immutable copy for debug UI and other consumers.
	class CpuProfiler
	{
	public:
		using Clock = std::chrono::steady_clock;

		static CpuProfiler& Get() noexcept;

		GGLAB_DELETE_COPYABLE_MOVABLE(CpuProfiler);

		void SetEnabled(bool enabled) noexcept;
		[[nodiscard]] bool IsEnabled() const noexcept;
		[[nodiscard]] bool IsCollecting() const noexcept;

		// Returns false when profiling is disabled or a frame is already active.
		[[nodiscard]] bool BeginFrame(uint64_t frameIndex) noexcept;
		void EndFrame() noexcept;

		[[nodiscard]] CpuProfileFrameSnapshot GetLatestFrame() const;

	private:
		friend class CpuProfileScope;

		CpuProfiler() = default;
		void RecordSample(std::string_view name,
			Clock::duration inclusive,
			Clock::duration self) noexcept;

	private:
		mutable std::mutex m_Mutex;
		std::atomic_bool m_Enabled = true;
		std::atomic_bool m_IsCollecting = false;
		uint64_t m_CurrentFrameIndex = 0;
		Clock::time_point m_FrameStart{};
		std::vector<CpuProfileSample> m_CurrentSamples;
		CpuProfileFrameSnapshot m_LatestFrame;
	};

	class CpuProfileScope
	{
	public:
		explicit CpuProfileScope(std::string_view name) noexcept;
		CpuProfileScope(CpuProfiler& profiler, std::string_view name) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(CpuProfileScope);
		~CpuProfileScope() noexcept;

	private:
		CpuProfiler* m_Profiler = nullptr;
		std::string_view m_Name;
		CpuProfiler::Clock::time_point m_Start{};
		CpuProfiler::Clock::duration m_ChildDuration{};
		CpuProfileScope* m_Parent = nullptr;
		bool m_IsActive = false;

		static thread_local CpuProfileScope* s_CurrentScope;
	};

	class CpuProfileFrameScope
	{
	public:
		explicit CpuProfileFrameScope(uint64_t frameIndex) noexcept;
		CpuProfileFrameScope(CpuProfiler& profiler, uint64_t frameIndex) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(CpuProfileFrameScope);
		~CpuProfileFrameScope() noexcept;

	private:
		CpuProfiler* m_Profiler = nullptr;
		bool m_IsActive = false;
	};
}

#define GGLAB_CPU_PROFILE_SCOPE(name) \
	::gglab::CpuProfileScope GGLAB_PROFILE_CONCAT(gglabCpuProfileScope_, __LINE__)(name)

#define GGLAB_CPU_PROFILE_FUNCTION() GGLAB_CPU_PROFILE_SCOPE(__FUNCTION__)

#define GGLAB_CPU_PROFILE_FRAME(frameIndex) \
	::gglab::CpuProfileFrameScope GGLAB_PROFILE_CONCAT(gglabCpuProfileFrame_, __LINE__)(frameIndex)
