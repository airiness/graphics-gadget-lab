#include "DiagnosticsContractSelfTests.h"

#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	struct DiagnosticsViewContractSnapshot
	{
		uint32_t m_CaptureSerial = 0;
	};

	template <> struct SnapshotTraits<DiagnosticsViewContractSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.DiagnosticsViewContractSnapshot");
	};

	struct UnregisteredDiagnosticsViewContractSnapshot
	{
	};

	template <> struct SnapshotTraits<UnregisteredDiagnosticsViewContractSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.UnregisteredDiagnosticsViewContractSnapshot");
	};

	namespace
	{
		class DiagnosticsViewContractProvider final : public SnapshotProviderBase
		{
		public:
			[[nodiscard]] SnapshotId GetId() const noexcept override
			{
				return SnapshotIdOf<DiagnosticsViewContractSnapshot>;
			}

			[[nodiscard]] std::string_view GetName() const noexcept override
			{
				return "Diagnostics View Contract";
			}

			void Capture(const SnapshotContext& snapshotContext, SnapshotStore& store) noexcept override
			{
				m_LastWorld = snapshotContext.m_World;
				store.GetOrCreate<DiagnosticsViewContractSnapshot>().m_CaptureSerial =
					++m_CaptureCount;
			}

			uint32_t m_CaptureCount = 0;
			World* m_LastWorld = nullptr;
		};

		class DiagnosticsLabSnapshotSource final : public LabSnapshotSourceBase
		{
		public:
			LabSnapshot GetLabSnapshot() const noexcept override
			{
				LabSnapshot snapshot{};
				snapshot.m_ActiveLabName = m_ActiveLabName;
				return snapshot;
			}

			std::string m_ActiveLabName;
		};
	}

	void RunDiagnosticsContractSelfTests(SelfTestContext& context) noexcept
	{
		DiagnosticsRuntime runtime;
		auto provider = std::make_unique<DiagnosticsViewContractProvider>();
		DiagnosticsViewContractProvider* providerObserver = provider.get();
		runtime.RegisterProvider(std::move(provider), SnapshotUpdatePolicy::OnDemand);
		DiagnosticsView& view = runtime;
		World firstWorld;
		World secondWorld;

		context.Check(view.GetSnapshot<UnregisteredDiagnosticsViewContractSnapshot>() == nullptr,
			"Diagnostics view returns no value for an unregistered snapshot contract");

		runtime.BeginFrame({ .m_World = &firstWorld });
		const DiagnosticsViewContractSnapshot* initial =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(initial && initial->m_CaptureSerial == 1 &&
				providerObserver->m_CaptureCount == 1 &&
				providerObserver->m_LastWorld == &firstWorld,
			"Diagnostics view lazily captures a registered immutable snapshot");

		const DiagnosticsViewContractSnapshot* cached =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(cached == initial && cached && cached->m_CaptureSerial == 1 &&
				providerObserver->m_CaptureCount == 1,
			"Diagnostics view reuses the published snapshot until refresh is requested");

		view.RequestRefresh<DiagnosticsViewContractSnapshot>();
		const DiagnosticsViewContractSnapshot* refreshed =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(refreshed == initial && refreshed && refreshed->m_CaptureSerial == 2 &&
				providerObserver->m_CaptureCount == 2,
			"Diagnostics view refresh requests recapture through the Runtime-owned provider");

		const auto profiles = view.GetProfiles();
		context.Check(profiles.size() == 1 && profiles.front().m_HasSnapshot &&
				profiles.front().m_CaptureCount == 2 &&
				profiles.front().m_CacheHitCount == 1 &&
				!profiles.front().m_RefreshPending,
			"Diagnostics view exposes immutable capture profile observations");

		runtime.EndFrame();
		view.RequestRefresh<DiagnosticsViewContractSnapshot>();
		const DiagnosticsViewContractSnapshot* closedFrameSnapshot =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(closedFrameSnapshot && closedFrameSnapshot == refreshed &&
				closedFrameSnapshot->m_CaptureSerial == 2 &&
				providerObserver->m_CaptureCount == 2 &&
				providerObserver->m_LastWorld == &firstWorld,
			"Closed diagnostics frames cannot capture through a borrowed context");

		runtime.BeginFrame({ .m_World = &secondWorld });
		const DiagnosticsViewContractSnapshot* nextFrameSnapshot =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(nextFrameSnapshot && nextFrameSnapshot == refreshed &&
				nextFrameSnapshot->m_CaptureSerial == 3 &&
				providerObserver->m_CaptureCount == 3 &&
				providerObserver->m_LastWorld == &secondWorld,
			"Pending refresh captures against the next active frame context");

		runtime.EndFrame();
		runtime.EndFrame();
		view.RequestRefresh<DiagnosticsViewContractSnapshot>();
		const DiagnosticsViewContractSnapshot* repeatedlyClosedSnapshot =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(repeatedlyClosedSnapshot &&
				repeatedlyClosedSnapshot == nextFrameSnapshot &&
				repeatedlyClosedSnapshot->m_CaptureSerial == 3 &&
				providerObserver->m_CaptureCount == 3,
			"Diagnostics frame closure is idempotent and preserves immutable publication");

		runtime.Reset();
		context.Check(view.GetSnapshot<DiagnosticsViewContractSnapshot>() == nullptr &&
				providerObserver->m_CaptureCount == 3,
			"Diagnostics reset clears publication without capturing outside a frame");

		DiagnosticsRuntime labDiagnostics;
		labDiagnostics.RegisterProvider(
			std::make_unique<LabSnapshotProvider>(), SnapshotUpdatePolicy::EveryFrame);
		DiagnosticsLabSnapshotSource firstLabSource;
		firstLabSource.m_ActiveLabName = "First Lab";
		labDiagnostics.BeginFrame({ .m_LabSnapshotSource = &firstLabSource });
		const LabSnapshot* firstLabSnapshot = labDiagnostics.GetSnapshot<LabSnapshot>();
		context.Check(firstLabSnapshot && firstLabSnapshot->m_ActiveLabName == "First Lab",
			"Lab diagnostics capture uses the Runtime-owned frame context source");

		labDiagnostics.EndFrame();
		DiagnosticsLabSnapshotSource secondLabSource;
		secondLabSource.m_ActiveLabName = "Second Lab";
		labDiagnostics.BeginFrame({ .m_LabSnapshotSource = &secondLabSource });
		const LabSnapshot* secondLabSnapshot = labDiagnostics.GetSnapshot<LabSnapshot>();
		context.Check(secondLabSnapshot && secondLabSnapshot == firstLabSnapshot &&
				secondLabSnapshot->m_ActiveLabName == "Second Lab",
			"Lab diagnostics recaptures from the current AppRuntime-supplied source");
		labDiagnostics.EndFrame();
	}
}
