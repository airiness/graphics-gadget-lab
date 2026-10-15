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
			uint64_t RequestNonFiniteScan(std::vector<NonFiniteScanTarget> targets, uint64_t version, uint64_t serial, bool, uint64_t) noexcept override
			{
				if (m_Busy)
				{
					return 0;
				}
				m_ScanTargets = std::move(targets);
				m_Version = version;
				m_Frame = serial;
				return ++m_Id;
			}
			std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override { return {}; }
			FrameCaptureAvailability GetRequestAvailability(uint32_t) const noexcept override { return FrameCaptureAvailability::Available; }
			FrameCaptureAvailability GetAvailability(RHIFormat, uint32_t, uint32_t) const noexcept override { return FrameCaptureAvailability::Available; }
			uint32_t GetUnfinishedRequestCount() const noexcept override { return 0; }
			bool HasNonFiniteInstrumentation() const noexcept override { return m_Instrumentation; }
			uint32_t GetNonFiniteScanTargetLimit() const noexcept override { return m_ScanLimit; }
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
				for (const auto& scan : m_ScanTargets)
				{
					NonFiniteScanBoundary boundary;
					boundary.m_Target.m_Address = scan.m_Address;
					boundary.m_Target.m_Subresources = { .m_BaseArraySlice = 2, .m_ArraySliceCount = 1 };
					boundary.m_Parameters = { .m_ScalarCount = 8, .m_Width = 2, .m_Height = 2, .m_Channels = 2 };
					for (uint32_t i = 0; i < 8; ++i)
					{
						AccumulateNonFiniteScanValue(boundary.m_Record, anomaly && i == 5 ? 0x7fc00000u : 0x3f800000u, i);
					}
					boundary.m_Result = EvaluateNonFiniteBoundary(boundary.m_Record, 8);
					payload->m_Boundaries.push_back(std::move(boundary));
				}
				return { .m_RequestId = m_Id, .m_Source = FrameCaptureSource::NonFiniteScan,
					.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = m_Frame, .m_NonFiniteScans = payload };
			}
			uint64_t m_Id = 0;
			std::vector<NonFiniteScanTarget> m_ScanTargets;
			uint64_t m_Version = 0;
			uint64_t m_Frame = 0;
			bool m_Busy = false;
			bool m_Instrumentation = false;
			uint32_t m_ScanLimit = 64;
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
		context.Check(watch.Configure(configuration, error) && watch.UsesDefaultTargets(),
			"Omitting addresses enables semantic default boundary coverage");
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
		{
			WatchCaptureControl defaultControl;
			NonFiniteWatchCoordinator defaultsWatch(&defaultControl);
			context.Check(defaultsWatch.Configure({ .m_Production = false }, error), "Default boundary watch configures without instrumentation");
			const auto& defaultStatus = defaultsWatch.GetStatus();
			defaultsWatch.PrepareFrame(1);
			context.Check(defaultControl.m_Id == 0 && defaultStatus.m_Targets.size() == NonFiniteWatchSemanticCount &&
				defaultStatus.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"Missing pipeline applicability reports every incomplete role and never submits an empty passing request");
			NonFiniteWatchDefaults defaults;
			for (size_t i = 0; i < defaults.size(); ++i)
			{
				defaults[i].m_Semantic = static_cast<NonFiniteWatchSemantic>(i);
				defaults[i].m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			defaultsWatch.PrepareFrame(2, false, defaults);
			context.Check(defaultControl.m_Id == 0 && defaultStatus.m_Boundary.m_Status == NonFiniteClaimStatus::NotApplicable,
				"An explicitly inactive pipeline reports not applicable without scanning");
			defaults[0].m_Ready = true;
			defaults[0].m_Claim = {};
			defaults[0].m_Address.m_Resource = "Motion";
			defaultsWatch.PrepareFrame(3, false, defaults);
			const auto firstFrame = defaultControl.Complete();
			defaults[0].m_Address.m_Resource = "DifferentMotion";
			defaultsWatch.PrepareFrame(4, false, defaults);
			const auto secondFrame = defaultControl.Complete(true);
			context.Check(defaultsWatch.OnScanResult(firstFrame) && defaultStatus.m_Boundary.m_Status == NonFiniteClaimStatus::Passed &&
				defaultStatus.m_Targets[0].m_Address.m_Resource == "Motion" && defaultStatus.m_Targets[1].m_Result.m_Claim.m_Status == NonFiniteClaimStatus::NotApplicable,
				"Delayed evidence uses the submitting frame's resolved targets and retains inactive roles");
			context.Check(defaultsWatch.OnScanResult(secondFrame) && defaultStatus.m_Boundary.m_Status == NonFiniteClaimStatus::Failed &&
				defaultStatus.m_FirstAnomaly->m_Target->m_Semantic == NonFiniteWatchSemantic::MotionVectors,
				"Default anomalies retain their semantic role across target changes");
			defaults[1].m_Claim = {};
			defaults[1].m_Failure = "MissingCoverage: active AO has no binding.";
			defaultsWatch.PrepareFrame(5, false, defaults);
			context.Check(defaultsWatch.OnScanResult(defaultControl.Complete()) && defaultStatus.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete &&
				defaultStatus.m_Targets[0].m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Passed &&
				defaultStatus.m_Failure.find("ambient-occlusion") != std::string::npos,
				"A clean applicable scan cannot hide another active role's missing coverage");
			defaultsWatch.PrepareFrame(6, true, defaults);
			context.Check(defaultStatus.m_Targets[0].m_Record.m_CheckedCount == 0 &&
				defaultStatus.m_Targets[2].m_Result.m_Claim.m_Status == NonFiniteClaimStatus::NotApplicable,
				"Capacity pressure clears scan evidence while preserving inactive feature classifications");
		}

		{
			WatchCaptureControl generations;
			generations.m_ScanLimit = 2;
			NonFiniteWatchCoordinator generationWatch(&generations);
			context.Check(generationWatch.Configure({ .m_Production = false }, error), "Generation watch configures");
			NonFiniteWatchDefaults defaults;
			for (size_t i = 0; i < defaults.size(); ++i)
			{
				defaults[i].m_Semantic = static_cast<NonFiniteWatchSemantic>(i);
				defaults[i].m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			auto& cube = defaults[static_cast<size_t>(NonFiniteWatchSemantic::Environment)];
			cube.m_Ready = true;
			cube.m_Generation = { { 100, 1 }, 1 };
			for (uint32_t mip = 0; mip < 3; ++mip)
			{
				cube.m_Mips.push_back({ .m_Resource = "Active.Cube", .m_Mip = mip, .m_SliceCount = 6 });
			}
			generationWatch.PrepareFrame(1, false, defaults);
			const auto first = generations.Complete();
			context.Check(generations.m_ScanTargets.size() == 2, "Generation work respects the shared scan target allowance");
			generationWatch.PrepareFrame(2, false, defaults);
			const auto tail = generations.Complete();
			context.Check(generations.m_ScanTargets.size() == 1 && generations.m_ScanTargets[0].m_Address.m_Mip == 2,
				"Pending generation mips are not duplicated while remaining mips advance");
			context.Check(generationWatch.OnScanResult(first) && generationWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"Partial generation completion is incomplete coverage");
			context.Check(generationWatch.OnScanResult(tail) && generationWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Passed,
				"A generation completes only after every admitted mip has valid results");
			generationWatch.PrepareFrame(3, false, defaults);
			const auto& generationStatus = generationWatch.GetStatus();
			context.Check(generations.m_Id == 2 && generationStatus.m_Boundary.m_Status == NonFiniteClaimStatus::NotApplicable &&
				generationStatus.m_Targets[4].m_CheckedFrameSerial == 1 && generationStatus.m_Targets[4].m_Record.m_CheckedCount == 8,
				"Unchanged content is not rescanned and retains its last checked frame and record");
			cube.m_Generation.m_Revision = 2;
			generationWatch.PrepareFrame(4, false, defaults);
			const auto superseded = generations.Complete(true);
			cube.m_Generation.m_Revision = 3;
			generationWatch.PrepareFrame(5, false, defaults);
			const auto current = generations.Complete();
			context.Check(generationWatch.OnScanResult(superseded) && generationStatus.m_FirstAnomaly &&
				generationStatus.m_FirstAnomaly->m_Target->m_Generation.m_Revision == 2,
				"A superseded generation's late anomaly remains diagnostic evidence");
			generationWatch.PrepareFrame(6, false, defaults);
			const auto currentTail = generations.Complete();
			context.Check(generationWatch.OnScanResult(current) && generationWatch.OnScanResult(currentTail),
				"Current generation completions drain independently of superseded work");
			generationWatch.PrepareFrame(7, false, defaults);
			context.Check(generations.m_Id == 5 && generationStatus.m_Targets[4].m_Generation.m_Revision == 3 &&
				generationStatus.m_Targets[4].m_Record.m_NanCount == 0 && generationStatus.m_Boundary.m_Status == NonFiniteClaimStatus::NotApplicable,
				"Old generation evidence cannot certify or overwrite the active generation");
			cube.m_Generation.m_Revision = 4;
			generationWatch.PrepareFrame(8, true, defaults);
			context.Check(generations.m_Id == 5 && generationStatus.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"Yielded generation work remains unverified and retryable");
			generationWatch.PrepareFrame(9, false, defaults);
			auto cancelled = generations.Complete();
			cancelled.m_Status = FrameCaptureStatus::Failed;
			context.Check(generationWatch.OnScanResult(cancelled) && generationStatus.m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"Failed transport cannot certify generation contents");
			generationWatch.PrepareFrame(10, false, defaults);
			context.Check(generations.m_Id == 7 && generations.m_ScanTargets.size() == 2,
				"Failed generation scan slots are released for a later retry");
			context.Check(generationWatch.Configure({ .m_Production = false }, error), "Generation configuration replaces atomically");
			generationWatch.PrepareFrame(11, false, defaults);
			context.Check(generations.m_Id == 8, "A new watch configuration rechecks already active content");
		}

		{
			WatchCaptureControl resourceControl;
			NonFiniteWatchCoordinator resourceWatch(&resourceControl);
			NonFiniteWatchDefaults defaults;
			for (size_t i = 0; i < defaults.size(); ++i)
			{
				defaults[i].m_Semantic = static_cast<NonFiniteWatchSemantic>(i);
				defaults[i].m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			auto& lut = defaults[static_cast<size_t>(NonFiniteWatchSemantic::AtmosphereSkyView)];
			lut.m_Ready = true;
			lut.m_Generation = { { 200, 1 }, 1 };
			lut.m_Mips = { RGResourceAddress{ .m_Resource = "SkyView", .m_SliceCount = 1 } };
			context.Check(resourceWatch.Configure({ .m_Production = false }, error), "Resource revision watch configures");
			resourceWatch.PrepareFrame(1, false, defaults);
			context.Check(resourceWatch.OnScanResult(resourceControl.Complete()), "Initial resource revision completes");
			resourceWatch.PrepareFrame(2, false, defaults);
			context.Check(resourceControl.m_Id == 1, "Completed resource contents are reused without another scan");
			lut.m_Generation.m_Texture = { 200, 2 };
			resourceWatch.PrepareFrame(3, false, defaults);
			context.Check(resourceControl.m_Id == 2 && resourceWatch.OnScanResult(resourceControl.Complete(true)),
				"Recycled physical handles require new evidence even when their content revision number repeats");
			resourceWatch.PrepareFrame(4, false, defaults);
			context.Check(resourceControl.m_Id == 2 && resourceWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Failed,
				"A failed active generation cannot become clean merely because it needs no repeat scan");
			context.Check(resourceWatch.Configure({ .m_Production = false }, error), "Completed generation watch reconfigures");
			resourceWatch.PrepareFrame(5, false, defaults);
			context.Check(resourceControl.m_Id == 3, "Replacing configuration invalidates completed generation evidence");
			lut.m_Mips[0].m_Mip = 2;
			resourceWatch.PrepareFrame(6, false, defaults);
			context.Check(resourceControl.m_Id == 3 && resourceWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"Malformed generation mip metadata is missing coverage and cannot index the completion cache");
		}

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
