#include "DiagnosticsContractSelfTests.h"

#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"

#include <cstdint>
#include <memory>

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

			void Capture(const SnapshotContext&, SnapshotStore& store) noexcept override
			{
				store.GetOrCreate<DiagnosticsViewContractSnapshot>().m_CaptureSerial =
					++m_CaptureCount;
			}

			uint32_t m_CaptureCount = 0;
		};
	}

	void RunDiagnosticsContractSelfTests(SelfTestContext& context) noexcept
	{
		DiagnosticsRuntime runtime;
		auto provider = std::make_unique<DiagnosticsViewContractProvider>();
		DiagnosticsViewContractProvider* providerObserver = provider.get();
		runtime.RegisterProvider(std::move(provider), SnapshotUpdatePolicy::OnDemand);
		DiagnosticsView& view = runtime;

		context.Check(view.GetSnapshot<UnregisteredDiagnosticsViewContractSnapshot>() == nullptr,
			"Diagnostics view returns no value for an unregistered snapshot contract");

		runtime.BeginFrame({});
		const DiagnosticsViewContractSnapshot* initial =
			view.GetSnapshot<DiagnosticsViewContractSnapshot>();
		context.Check(initial && initial->m_CaptureSerial == 1 &&
				providerObserver->m_CaptureCount == 1,
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
	}
}
