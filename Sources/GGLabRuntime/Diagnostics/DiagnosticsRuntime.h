#pragma once

#include "Diagnostics/SnapshotContext.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DiagnosticsRuntime final : public DiagnosticsView
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
			std::unique_ptr<SnapshotProviderBase> provider, SnapshotUpdatePolicy policy) noexcept;
		void BeginFrame(const SnapshotContext& context) noexcept;
		void EndFrame() noexcept;
		void Reset() noexcept;

		template <typename T> void Invalidate() noexcept
		{
			if (ProviderRuntime* runtime = FindProvider(SnapshotIdOf<T>))
			{
				runtime->m_Dirty = true;
			}
		}

		using DiagnosticsView::RequestRefresh;
		void RequestRefresh(SnapshotId id) noexcept override;
		[[nodiscard]] std::vector<SnapshotProfile> GetProfiles() const override;

	private:
		[[nodiscard]] const void* GetSnapshotData(SnapshotId id) noexcept override;
		[[nodiscard]] ProviderRuntime* FindProvider(SnapshotId id) noexcept;
		void Capture(ProviderRuntime& runtime) noexcept;

		SnapshotContext m_Context{};
		SnapshotStore m_Store;
		std::vector<ProviderRuntime> m_Providers;
		uint64_t m_FrameIndex = 0;
		bool m_FrameOpen = false;
	};
}
