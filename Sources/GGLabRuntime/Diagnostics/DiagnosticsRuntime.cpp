#include "Diagnostics/DiagnosticsRuntime.h"

#include <algorithm>
#include <chrono>

namespace gglab
{
	void DiagnosticsRuntime::RegisterProvider(
		std::unique_ptr<SnapshotProviderBase> provider, SnapshotUpdatePolicy policy) noexcept
	{
		GGLAB_ASSERT(provider);
		GGLAB_ASSERT(!FindProvider(provider->GetId()));
		if (!provider || FindProvider(provider->GetId()))
		{
			return;
		}

		ProviderRuntime runtime{};
		runtime.m_Profile.m_Id = provider->GetId();
		runtime.m_Profile.m_Name = provider->GetName();
		runtime.m_Profile.m_Policy = policy;
		runtime.m_Provider = std::move(provider);
		m_Providers.push_back(std::move(runtime));
	}

	void DiagnosticsRuntime::BeginFrame(const SnapshotContext& context) noexcept
	{
		GGLAB_ASSERT_MSG(!m_FrameOpen,
			"DiagnosticsRuntime::BeginFrame called before the previous frame ended.");
		m_Context = context;
		m_FrameOpen = true;
		++m_FrameIndex;
		for (auto& runtime : m_Providers)
		{
			if (runtime.m_Profile.m_RefreshPending)
			{
				Capture(runtime);
			}
		}
	}

	void DiagnosticsRuntime::EndFrame() noexcept
	{
		m_Context = {};
		m_FrameOpen = false;
	}

	void DiagnosticsRuntime::Reset() noexcept
	{
		EndFrame();
		m_Store.Clear();
		m_FrameIndex = 0;
		for (auto& runtime : m_Providers)
		{
			runtime.m_Profile.m_LastCaptureMilliseconds = 0.0;
			runtime.m_Profile.m_AverageCaptureMilliseconds = 0.0;
			runtime.m_Profile.m_MaxCaptureMilliseconds = 0.0;
			runtime.m_Profile.m_CaptureCount = 0;
			runtime.m_Profile.m_CacheHitCount = 0;
			runtime.m_Profile.m_LastCaptureFrame = 0;
			runtime.m_Profile.m_HasSnapshot = false;
			runtime.m_Profile.m_RefreshPending = false;
			runtime.m_TotalCaptureMilliseconds = 0.0;
			runtime.m_Dirty = true;
		}
	}

	const void* DiagnosticsRuntime::GetSnapshotData(SnapshotId id) noexcept
	{
		ProviderRuntime* runtime = FindProvider(id);
		if (!runtime)
		{
			return nullptr;
		}

		const bool missing = !m_Store.Contains(runtime->m_Profile.m_Id);
		const bool capture =
			missing ||
			(runtime->m_Profile.m_Policy == SnapshotUpdatePolicy::EveryFrame &&
				runtime->m_Profile.m_LastCaptureFrame != m_FrameIndex) ||
			(runtime->m_Profile.m_Policy != SnapshotUpdatePolicy::ManualRefresh &&
				runtime->m_Dirty) ||
			runtime->m_Profile.m_RefreshPending;
		if (capture)
		{
			if (m_FrameOpen)
			{
				Capture(*runtime);
			}
		}
		else
		{
			++runtime->m_Profile.m_CacheHitCount;
		}
		return m_Store.Get(runtime->m_Profile.m_Id);
	}

	void DiagnosticsRuntime::RequestRefresh(SnapshotId id) noexcept
	{
		if (ProviderRuntime* runtime = FindProvider(id))
		{
			runtime->m_Profile.m_RefreshPending = true;
		}
	}

	std::vector<SnapshotProfile> DiagnosticsRuntime::GetProfiles() const
	{
		std::vector<SnapshotProfile> profiles;
		profiles.reserve(m_Providers.size());
		for (const auto& runtime : m_Providers)
		{
			profiles.push_back(runtime.m_Profile);
		}
		return profiles;
	}

	DiagnosticsRuntime::ProviderRuntime* DiagnosticsRuntime::FindProvider(SnapshotId id) noexcept
	{
		const auto iterator = std::find_if(m_Providers.begin(), m_Providers.end(),
			[id](const ProviderRuntime& runtime) { return runtime.m_Profile.m_Id == id; });
		return iterator == m_Providers.end() ? nullptr : &*iterator;
	}

	void DiagnosticsRuntime::Capture(ProviderRuntime& runtime) noexcept
	{
		GGLAB_ASSERT_MSG(m_FrameOpen,
			"DiagnosticsRuntime cannot capture without an active borrowed frame context.");
		if (!m_FrameOpen)
		{
			return;
		}

		const auto begin = std::chrono::steady_clock::now();
		runtime.m_Provider->Capture(m_Context, m_Store);
		const auto end = std::chrono::steady_clock::now();
		const double milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();

		runtime.m_TotalCaptureMilliseconds += milliseconds;
		++runtime.m_Profile.m_CaptureCount;
		runtime.m_Profile.m_LastCaptureMilliseconds = milliseconds;
		runtime.m_Profile.m_AverageCaptureMilliseconds =
			runtime.m_TotalCaptureMilliseconds /
			static_cast<double>(runtime.m_Profile.m_CaptureCount);
		runtime.m_Profile.m_MaxCaptureMilliseconds =
			std::max(runtime.m_Profile.m_MaxCaptureMilliseconds, milliseconds);
		runtime.m_Profile.m_LastCaptureFrame = m_FrameIndex;
		runtime.m_Profile.m_HasSnapshot = m_Store.Contains(runtime.m_Profile.m_Id);
		runtime.m_Profile.m_RefreshPending = false;
		runtime.m_Dirty = false;
	}
}
