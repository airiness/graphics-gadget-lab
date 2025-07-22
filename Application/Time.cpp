#include "Precompiled.h"
#include "Time.h"

namespace graphicsGadgetLab
{
	void Time::Initialize() noexcept
	{
		m_StartTime = std::chrono::high_resolution_clock::now();
		m_LastTime = m_StartTime;
	}

	void Time::Update() noexcept
	{
		auto currentTime = std::chrono::high_resolution_clock::now();

		m_DeltaTime = std::chrono::duration<double>(currentTime - m_LastTime).count();
		m_TotalTime = std::chrono::duration<double>(currentTime - m_StartTime).count();

		m_LastTime = currentTime;

		m_FrameCount++;

		m_FpsTimer += m_DeltaTime;
		if (m_FpsTimer >= 1.0)
		{
			m_Fps = m_FpsCounter / m_FpsTimer;

			m_FpsTimer = 0.0;
			m_FpsCounter = 0;
		}
	}
}
