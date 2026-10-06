#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace gglab
{
	class Time
	{
	public:
		Time() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(Time);
		~Time() = default;

		void Initialize() noexcept;
		void Update() noexcept;

		// A fixed step makes every Update advance delta and total time by exactly
		// that many seconds, independent of wall-clock frame pacing. The FPS
		// estimate keeps measuring wall-clock time.
		void SetFixedDeltaTime(std::optional<double> seconds) noexcept;
		[[nodiscard]] std::optional<double> GetFixedDeltaTime() const noexcept
		{
			return m_FixedDeltaTime;
		}

		double GetDeltaTime() const noexcept { return m_DeltaTime; }
		double GetTotalTime() const noexcept { return m_TotalTime; }
		double GetFps() const noexcept { return m_Fps; }
		uint64_t GetFrameCount() const noexcept { return m_FrameCount; }

	private:
		std::chrono::high_resolution_clock::time_point m_StartTime;
		std::chrono::high_resolution_clock::time_point m_LastTime;

		double m_DeltaTime = 0.0;
		double m_TotalTime = 0.0;
		std::optional<double> m_FixedDeltaTime;

		double m_Fps = 0.0;
		double m_FpsTimer = 0.0;
		uint64_t m_FpsCounter = 0;

		uint64_t m_FrameCount = 0;
	};
}
