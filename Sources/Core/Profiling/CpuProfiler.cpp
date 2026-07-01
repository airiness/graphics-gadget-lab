#include "Core/Precompiled.h"
#include "Core/Profiling/CpuProfiler.h"

namespace gglab
{
	thread_local CpuProfileScope* CpuProfileScope::s_CurrentScope = nullptr;

	CpuProfiler& CpuProfiler::Get() noexcept
	{
		static CpuProfiler profiler;
		return profiler;
	}

	void CpuProfiler::SetEnabled(bool enabled) noexcept
	{
		m_Enabled.store(enabled, std::memory_order_relaxed);
	}

	bool CpuProfiler::IsEnabled() const noexcept
	{
		return m_Enabled.load(std::memory_order_relaxed);
	}

	bool CpuProfiler::IsCollecting() const noexcept
	{
		return m_IsCollecting.load(std::memory_order_acquire);
	}

	bool CpuProfiler::BeginFrame(uint64_t frameIndex) noexcept
	{
		if (!IsEnabled() || frameIndex == 0)
		{
			return false;
		}

		std::scoped_lock lock(m_Mutex);
		if (m_IsCollecting.load(std::memory_order_relaxed))
		{
			GGLAB_ASSERT_MSG(false, "CpuProfiler::BeginFrame called while another frame is active.");
			return false;
		}

		m_CurrentFrameIndex = frameIndex;
		m_CurrentSamples.clear();
		m_FrameStart = Clock::now();
		m_IsCollecting.store(true, std::memory_order_release);
		return true;
	}

	void CpuProfiler::EndFrame() noexcept
	{
		if (!IsCollecting())
		{
			return;
		}

		const auto frameEnd = Clock::now();
		std::scoped_lock lock(m_Mutex);
		if (!m_IsCollecting.load(std::memory_order_relaxed))
		{
			return;
		}

		m_IsCollecting.store(false, std::memory_order_release);
		m_LatestFrame.m_FrameIndex = m_CurrentFrameIndex;
		m_LatestFrame.m_FrameMilliseconds =
			std::chrono::duration<double, std::milli>(frameEnd - m_FrameStart).count();
		m_LatestFrame.m_Samples = m_CurrentSamples;
	}

	CpuProfileFrameSnapshot CpuProfiler::GetLatestFrame() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_LatestFrame;
	}

	void CpuProfiler::RecordSample(std::string_view name,
		Clock::duration inclusive,
		Clock::duration self) noexcept
	{
		if (name.empty() || !IsCollecting())
		{
			return;
		}

		std::scoped_lock lock(m_Mutex);
		if (!m_IsCollecting.load(std::memory_order_relaxed))
		{
			return;
		}

		auto sampleIter = std::ranges::find_if(m_CurrentSamples,
			[name](const CpuProfileSample& sample)
			{
				return sample.m_Name == name;
			});
		if (sampleIter == m_CurrentSamples.end())
		{
			m_CurrentSamples.push_back({ .m_Name = std::string(name) });
			sampleIter = std::prev(m_CurrentSamples.end());
		}

		sampleIter->m_InclusiveMilliseconds +=
			std::chrono::duration<double, std::milli>(inclusive).count();
		sampleIter->m_SelfMilliseconds +=
			std::chrono::duration<double, std::milli>(self).count();
		++sampleIter->m_CallCount;
	}

	CpuProfileScope::CpuProfileScope(std::string_view name) noexcept :
		CpuProfileScope(CpuProfiler::Get(), name)
	{}

	CpuProfileScope::CpuProfileScope(CpuProfiler& profiler, std::string_view name) noexcept :
		m_Profiler(&profiler),
		m_Name(name)
	{
		if (name.empty() || !profiler.IsCollecting())
		{
			return;
		}

		m_Parent = s_CurrentScope;
		s_CurrentScope = this;
		m_Start = CpuProfiler::Clock::now();
		m_IsActive = true;
	}

	CpuProfileScope::~CpuProfileScope() noexcept
	{
		if (!m_IsActive)
		{
			return;
		}

		const auto duration = CpuProfiler::Clock::now() - m_Start;
		GGLAB_ASSERT_MSG(s_CurrentScope == this, "CPU profile scopes must end in LIFO order.");
		s_CurrentScope = m_Parent;

		if (m_Parent && m_Parent->m_IsActive && m_Parent->m_Profiler == m_Profiler)
		{
			m_Parent->m_ChildDuration += duration;
		}

		const auto selfDuration = duration >= m_ChildDuration ?
			duration - m_ChildDuration : CpuProfiler::Clock::duration::zero();
		m_Profiler->RecordSample(m_Name, duration, selfDuration);
	}

	CpuProfileFrameScope::CpuProfileFrameScope(uint64_t frameIndex) noexcept :
		CpuProfileFrameScope(CpuProfiler::Get(), frameIndex)
	{}

	CpuProfileFrameScope::CpuProfileFrameScope(CpuProfiler& profiler, uint64_t frameIndex) noexcept :
		m_Profiler(&profiler),
		m_IsActive(profiler.BeginFrame(frameIndex))
	{}

	CpuProfileFrameScope::~CpuProfileFrameScope() noexcept
	{
		if (m_IsActive)
		{
			m_Profiler->EndFrame();
		}
	}
}
