#pragma once

namespace graphicsGadgetLab
{
	class Time
	{
	public:
		Time() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(Time);
		~Time() = default;

		void Initialize() noexcept;
		void Update() noexcept;

		double GetDeltaTime() const noexcept { return m_DeltaTime; }
		double GetTotalTime() const noexcept { return m_TotalTime; }
		double GetFps() const noexcept { return m_Fps; }
		uint64_t GetFrameCount() const noexcept { return m_FrameCount; }

	private:
		std::chrono::high_resolution_clock::time_point m_StartTime;
		std::chrono::high_resolution_clock::time_point m_LastTime;

		double m_DeltaTime = 0.0;
		double m_TotalTime = 0.0;

		double m_Fps = 0.0;
		double m_FpsTimer = 0.0;
		uint64_t m_FpsCounter = 0;

		uint64_t m_FrameCount = 0;

	};
}