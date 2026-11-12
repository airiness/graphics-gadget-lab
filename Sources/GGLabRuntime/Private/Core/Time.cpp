#include "GGLabRuntime/Core/Time.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cmath>

namespace gglab
{
	void Time::Initialize() noexcept
	{
		m_StartTime = std::chrono::high_resolution_clock::now();
		m_LastTime = m_StartTime;
	}

	void Time::Update() noexcept
	{
		auto currentTime = std::chrono::high_resolution_clock::now();

		const double wallDeltaTime =
			std::chrono::duration<double>(currentTime - m_LastTime).count();
		if (m_FixedDeltaTime)
		{
			m_DeltaTime = *m_FixedDeltaTime;
			m_TotalTime += *m_FixedDeltaTime;
		}
		else
		{
			m_DeltaTime = wallDeltaTime;
			m_TotalTime = std::chrono::duration<double>(currentTime - m_StartTime).count();
		}

		m_LastTime = currentTime;

		m_FrameCount++;
	}

	void Time::Hold() noexcept
	{
		m_LastTime = std::chrono::high_resolution_clock::now();
		m_DeltaTime = 0.0;
		m_FrameCount++;
	}

	void Time::SetFixedDeltaTime(std::optional<double> seconds) noexcept
	{
		GGLAB_ASSERT_MSG(!seconds || (std::isfinite(*seconds) && *seconds > 0.0),
			"A fixed time step must be finite and positive.");
		m_FixedDeltaTime = seconds && std::isfinite(*seconds) && *seconds > 0.0 ?
			seconds :
			std::nullopt;
	}
}
