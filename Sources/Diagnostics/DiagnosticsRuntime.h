#pragma once

#include "Diagnostics/SnapshotContext.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DiagnosticsRuntime
	{
	private:
		struct ProviderRuntime
		{
			std::unique_ptr<SnapshotProviderBase> m_Provider;
			SnapshotProfile m_Profile{};
			double m_TotalCaptureMilliseconds = 0.0;
			bool m_Dirty = true;
		};

	public:
		DiagnosticsRuntime() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DiagnosticsRuntime);
		~DiagnosticsRuntime() = default;

		void RegisterProvider(
			std::unique_ptr<SnapshotProviderBase> provider,
			SnapshotUpdatePolicy policy) noexcept;
		void BeginFrame(const SnapshotContext& context) noexcept;
		void Reset() noexcept;

		template<typename T>
		[[nodiscard]] const T* GetSnapshot() noexcept
		{
			ProviderRuntime* runtime = FindProvider(SnapshotIdOf<T>);
			if (!runtime)
			{
				return nullptr;
			}

			const bool missing = !m_Store.Contains(runtime->m_Profile.m_Id);
			const bool capture = missing ||
				(runtime->m_Profile.m_Policy == SnapshotUpdatePolicy::EveryFrame &&
					runtime->m_Profile.m_LastCaptureFrame != m_FrameIndex) ||
				(runtime->m_Profile.m_Policy != SnapshotUpdatePolicy::ManualRefresh && runtime->m_Dirty) ||
				runtime->m_Profile.m_RefreshPending;
			if (capture)
			{
				Capture(*runtime);
			}
			else
			{
				++runtime->m_Profile.m_CacheHitCount;
			}
			return m_Store.Get<T>();
		}

		template<typename T>
		void Invalidate() noexcept
		{
			if (ProviderRuntime* runtime = FindProvider(SnapshotIdOf<T>))
			{
				runtime->m_Dirty = true;
			}
		}

		template<typename T>
		void RequestRefresh() noexcept { RequestRefresh(SnapshotIdOf<T>); }

		void RequestRefresh(SnapshotId id) noexcept;
		[[nodiscard]] std::vector<SnapshotProfile> GetProfiles() const;

	private:
		[[nodiscard]] ProviderRuntime* FindProvider(SnapshotId id) noexcept;
		void Capture(ProviderRuntime& runtime) noexcept;

		SnapshotContext m_Context{};
		SnapshotStore m_Store;
		std::vector<ProviderRuntime> m_Providers;
		uint64_t m_FrameIndex = 0;
	};
}
