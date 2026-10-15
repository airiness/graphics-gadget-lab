#include "NonFiniteWatchCoordinatorSelfTests.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "Capture/FrameCaptureCoordinator.h"

#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		class WatchCaptureControl final : public FrameCaptureControlBase
		{
		public:
			uint64_t RequestCapture(FrameCaptureSource, uint64_t, uint64_t) noexcept override { return 0; }
			uint64_t RequestDiagnosticCapture(PostProcessDebugTap, uint64_t, uint64_t) noexcept override { return 0; }
			uint64_t RequestNonFiniteScan(std::vector<NonFiniteScanTarget>, uint64_t version, uint64_t serial, bool, uint64_t) noexcept override
			{
				if (m_Busy)
				{
					return 0;
				}
				m_Version = version;
				m_Frame = serial;
				return ++m_Id;
			}
			std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override { return {}; }
			FrameCaptureAvailability GetRequestAvailability(uint32_t) const noexcept override { return FrameCaptureAvailability::Available; }
			FrameCaptureAvailability GetAvailability(RHIFormat, uint32_t, uint32_t) const noexcept override { return FrameCaptureAvailability::Available; }
			uint32_t GetUnfinishedRequestCount() const noexcept override { return 0; }
			bool HasNonFiniteInstrumentation() const noexcept override { return m_Instrumentation; }
			void ConsumeResults(std::vector<FrameCaptureResult>& results) noexcept override
			{
				results.insert(results.end(), std::make_move_iterator(m_Results.begin()), std::make_move_iterator(m_Results.end()));
				m_Results.clear();
			}
			void ConsumeNonFiniteProductionFrames(std::vector<std::shared_ptr<const NonFiniteProductionFrame>>& frames) noexcept override
			{
				frames.insert(frames.end(), std::make_move_iterator(m_Production.begin()), std::make_move_iterator(m_Production.end()));
				m_Production.clear();
			}
			FrameCaptureResult Complete(bool anomaly = false) const
			{
				auto payload = std::make_shared<NonFiniteScanPayload>();
				payload->m_ConfigurationVersion = m_Version;
				NonFiniteScanBoundary boundary;
				boundary.m_Target.m_Address.m_Resource = "Motion";
				boundary.m_Target.m_Subresources = { .m_BaseArraySlice = 2, .m_ArraySliceCount = 1 };
				boundary.m_Parameters = { .m_ScalarCount = 8, .m_Width = 2, .m_Height = 2, .m_Channels = 2 };
				for (uint32_t i = 0; i < 8; ++i)
				{
					AccumulateNonFiniteScanValue(boundary.m_Record, anomaly && i == 5 ? 0x7fc00000u : 0x3f800000u, i);
				}
				boundary.m_Result = EvaluateNonFiniteBoundary(boundary.m_Record, 8);
				payload->m_Boundaries.push_back(std::move(boundary));
				return { .m_RequestId = m_Id, .m_Source = FrameCaptureSource::NonFiniteScan,
					.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = m_Frame, .m_NonFiniteScans = payload };
			}
			uint64_t m_Id = 0;
			uint64_t m_Version = 0;
			uint64_t m_Frame = 0;
			bool m_Busy = false;
			bool m_Instrumentation = false;
			std::vector<FrameCaptureResult> m_Results;
			std::vector<std::shared_ptr<const NonFiniteProductionFrame>> m_Production;
		};
	}

	void RunNonFiniteWatchCoordinatorSelfTests(SelfTestContext& context) noexcept
	{
		WatchCaptureControl control;
		NonFiniteWatchCoordinator watch(&control);
		std::string error;
		NonFiniteWatchConfiguration configuration{ .m_Production = false,
			.m_Targets = { NonFiniteScanTarget{ .m_Address = { .m_Resource = "Motion" } } } };
		context.Check(!watch.GetStatus().m_Enabled && watch.Configure(configuration, error) && error.empty(),
			"Watch is opt-in and explicit boundary report configuration needs no counter capability");
		context.Check(watch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
			"Configuring watch never establishes clean boundary evidence before a completed readback");
		watch.PrepareFrame(10);
		watch.PrepareFrame(10);
		context.Check(control.m_Id == 1 && control.m_Frame == 10 && control.m_Version == 1,
			"Watch queues one exact-frame scan per configuration and render frame");
		context.Check(watch.OnScanResult(control.Complete(true)), "Watch consumes its own Runtime result");
		const auto& status = watch.GetStatus();
		context.Check(status.m_Boundary.m_Status == NonFiniteClaimStatus::Failed && status.m_FirstAnomaly && status.m_FirstAnomaly->m_FrameSerial == 10,
			"A non-finite boundary records the first failing frame");
		context.Check(status.m_Targets[0].m_TextureLocation && status.m_Targets[0].m_TextureLocation->m_X == 0 &&
			status.m_Targets[0].m_TextureLocation->m_Y == 1 && status.m_Targets[0].m_TextureLocation->m_Channel == 1 && status.m_Targets[0].m_TextureLocation->m_Slice == 2,
			"Texture anomaly locations retain the addressed array slice and scalar channel");
		watch.PrepareFrame(11);
		context.Check(watch.OnScanResult(control.Complete()) && status.m_Boundary.m_Status == NonFiniteClaimStatus::Passed && status.m_FirstAnomaly->m_FrameSerial == 10,
			"Later clean frames update the latest report without erasing the first anomaly");
		watch.PrepareFrame(12);
		const auto old = control.Complete(true);
		context.Check(watch.Configure(configuration, error) && status.m_ConfigurationVersion == 2 && !status.m_FirstAnomaly,
			"Replacing configuration increments its identity and resets diagnostic history atomically");
		context.Check(watch.OnScanResult(old) && !status.m_FirstAnomaly && status.m_BoundaryFrameSerial == 0,
			"Late results of the previous configuration are drained without contaminating the new one");
		configuration.m_Mode = NonFiniteWatchMode::Fail;
		configuration.m_Production = true;
		context.Check(!watch.Configure(configuration, error) && error.starts_with("RequiresRestart") && status.m_ConfigurationVersion == 2,
			"Production fail mode without startup capability rejects without changing active watch");
		control.m_Instrumentation = true;
		context.Check(!watch.Configure(configuration, error) && error.starts_with("FailModeUnavailable"),
			"A report-only implementation cannot claim sequence fail-mode completion");
		configuration.m_Mode = NonFiniteWatchMode::Report;
		configuration.m_Targets.clear();
		context.Check(!watch.Configure(configuration, error) && error.starts_with("DefaultTargetsUnavailable"),
			"Unimplemented semantic defaults are explicit rather than an empty passing boundary list");
		configuration.m_Boundary = false;
		context.Check(watch.Configure(configuration, error), "Production-only report watch needs no boundary targets");
		NonFiniteProductionFrame production{ .m_FrameSerial = 11, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None } };
		watch.OnProductionFrame(production);
		context.Check(status.m_ProductionFrameSerial == 0, "A delayed counter frame before configuration cannot establish new production coverage");
		production.m_FrameSerial = 13;
		production.m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced };
		production.m_Sites.push_back({ .m_ProgramIndex = 2, .m_SiteIndex = 3, .m_Claim = production.m_Claim });
		control.m_Production.push_back(std::make_shared<NonFiniteProductionFrame>(production));
		production.m_FrameSerial = 14;
		production.m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
		production.m_Sites.clear();
		control.m_Production.push_back(std::make_shared<NonFiniteProductionFrame>(production));
		FrameCaptureCoordinator capture({ .m_Capture = &control, .m_NonFiniteWatch = &watch });
		capture.Update();
		context.Check(status.m_ProductionFrameSerial == 14 && status.m_Production.m_Status == NonFiniteClaimStatus::Passed &&
			status.m_FirstAnomaly && status.m_FirstAnomaly->m_FrameSerial == 13 && status.m_FirstAnomaly->m_SiteIndex == 3u,
			"One completion poll retains an anomalous counter frame even when followed by a clean frame");
		production.m_FrameSerial = 15;
		production.m_Claim = {};
		production.m_Failure = "Counter readback mapping failed";
		watch.OnProductionFrame(production);
		context.Check(status.m_Production.m_Status == NonFiniteClaimStatus::Incomplete && status.m_ProductionFailure == production.m_Failure && status.m_FirstAnomaly->m_FrameSerial == 13,
			"Incomplete production transport retains its reason without erasing the first anomaly");
		configuration.m_Boundary = true;
		configuration.m_Production = false;
		configuration.m_Targets = { NonFiniteScanTarget{ .m_Address = { .m_Resource = "Motion" } } };
		context.Check(watch.Configure(configuration, error), "Watch can return to explicit boundary reporting");
		watch.PrepareFrame(15);
		auto mismatched = control.Complete();
		mismatched.m_FrameSerial = 16;
		context.Check(watch.OnScanResult(mismatched) && status.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
			"A successful transport with a wrong frame identity is incomplete evidence");
		watch.PrepareFrame(16);
		control.m_Results.push_back(control.Complete(true));
		capture.Update();
		context.Check(status.m_Boundary.m_Status == NonFiniteClaimStatus::Failed,
			"The sole capture result consumer routes watch scans before capture encoding");
		watch.PrepareFrame(17, true);
		context.Check(control.m_Frame == 16 && status.m_BoundaryFrameSerial == 17 && status.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
			"Continuous report scans yield shared capacity to waiting resource exports without claiming coverage");
		watch.PrepareFrame(18);
		auto buffer = control.Complete(true);
		auto bufferPayload = std::make_shared<NonFiniteScanPayload>(*buffer.m_NonFiniteScans);
		bufferPayload->m_Boundaries[0].m_Target.m_Type = RGResourceType::RGBuffer;
		bufferPayload->m_Boundaries[0].m_BufferLayout.m_Components = { { 4, 4 },{ 12, 2 } };
		buffer.m_NonFiniteScans = bufferPayload;
		context.Check(watch.OnScanResult(buffer) && status.m_Targets[0].m_Element == 2u && status.m_Targets[0].m_ByteOffset == 12u,
			"Buffer anomalies identify the valid element and floating-point byte offset");
		watch.PrepareFrame(19);
		auto stale = control.Complete();
		auto stalePayload = std::make_shared<NonFiniteScanPayload>(*stale.m_NonFiniteScans);
		stalePayload->m_ConfigurationVersion = 0;
		stale.m_NonFiniteScans = stalePayload;
		context.Check(watch.OnScanResult(stale) && status.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
			"A payload configuration mismatch never passes despite a matching transport frame");
		watch.PrepareFrame(20);
		auto partial = control.Complete();
		auto partialPayload = std::make_shared<NonFiniteScanPayload>(*partial.m_NonFiniteScans);
		partialPayload->m_Boundaries.clear();
		partial.m_NonFiniteScans = partialPayload;
		context.Check(watch.OnScanResult(partial) && status.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
			"Missing target records cannot establish a complete boundary report");
		watch.PrepareFrame(21);
		auto wrongAddress = control.Complete();
		auto wrongAddressPayload = std::make_shared<NonFiniteScanPayload>(*wrongAddress.m_NonFiniteScans);
		wrongAddressPayload->m_Boundaries[0].m_Target.m_Address.m_Mip = 1;
		wrongAddress.m_NonFiniteScans = wrongAddressPayload;
		context.Check(watch.OnScanResult(wrongAddress) && status.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
			"A record for a different addressed subresource cannot establish coverage");
		control.m_Busy = true;
		watch.PrepareFrame(22);
		context.Check(status.m_BoundaryFrameSerial == 22 && status.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete &&
			status.m_Targets[0].m_Record.m_CheckedCount == 0,
			"Watch admission pressure reports missing coverage and clears stale per-target values");
		watch.PrepareFrame(23);
		context.Check(watch.Configure({ .m_Enabled = false }, error) && !status.m_Enabled && !status.m_FirstAnomaly,
			"Disabling watch clears active reporting and anomaly history");
		const auto id = control.m_Id;
		watch.PrepareFrame(24);
		context.Check(control.m_Id == id && !watch.OnScanResult({ .m_RequestId = 1000, .m_Source = FrameCaptureSource::NonFiniteScan }),
			"Disabled watch queues no scan and never steals unrelated Runtime request ids");
	}
}
