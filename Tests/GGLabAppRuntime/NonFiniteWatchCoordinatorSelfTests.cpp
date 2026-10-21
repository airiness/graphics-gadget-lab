#include "NonFiniteWatchCoordinatorSelfTests.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"

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
			uint64_t RequestCapture(FrameCaptureSource source, uint64_t serial, uint64_t) noexcept override
			{
				m_CaptureSource = source;
				m_CaptureSerial = serial;
				return ++m_CaptureId;
			}
			uint64_t RequestDiagnosticCapture(PostProcessDebugTap, uint64_t, uint64_t) noexcept override { return 0; }
			uint64_t RequestNonFiniteScan(std::vector<NonFiniteScanTarget> targets, uint64_t version, uint64_t serial, bool, uint64_t, bool defer) noexcept override
			{
				if (m_Busy)
				{
					return 0;
				}
				m_ScanTargets = std::move(targets);
				m_Version = version;
				m_Frame = serial;
				m_DeferFrame = defer;
				return ++m_Id;
			}
			std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override { return {}; }
			FrameCaptureAvailability GetRequestAvailability(uint32_t) const noexcept override { return FrameCaptureAvailability::Available; }
			FrameCaptureAvailability GetAvailability(RHIFormat, uint32_t, uint32_t) const noexcept override { return FrameCaptureAvailability::Available; }
			uint32_t GetUnfinishedRequestCount() const noexcept override { return 0; }
			bool HasNonFiniteInstrumentation() const noexcept override { return m_Instrumentation; }
			bool SetNonFiniteInjection(std::optional<NonFiniteInjection> injection) noexcept override
			{
				if (!m_Instrumentation)
				{
					return false;
				}
				m_Injection = injection;
				return true;
			}
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
			uint64_t m_CaptureId = 1000;
			uint64_t m_CaptureSerial = 0;
			FrameCaptureSource m_CaptureSource = FrameCaptureSource::Scene;
			std::vector<NonFiniteScanTarget> m_ScanTargets;
			uint64_t m_Version = 0;
			uint64_t m_Frame = 0;
			bool m_Busy = false;
			bool m_DeferFrame = false;
			bool m_Instrumentation = false;
			std::optional<NonFiniteInjection> m_Injection;
			uint32_t m_ScanLimit = 64;
			std::vector<FrameCaptureResult> m_Results;
			std::vector<std::shared_ptr<const NonFiniteProductionFrame>> m_Production;
		};

		void RunCoverageTests(SelfTestContext& context)
		{
			WatchCaptureControl control;
			control.m_Instrumentation = true;
			NonFiniteWatchCoordinator watch(&control);
			std::string error;
			NonFiniteWatchConfiguration configuration{ .m_Mode = NonFiniteWatchMode::Fail,
				.m_Targets = { NonFiniteScanTarget{ .m_Address = { .m_Resource = "Motion" } } } };
			context.Check(watch.Configure(configuration, error), "Range fail mode configures with both claims");
			const uint64_t range = watch.OpenCoverage();
			watch.PrepareFrame(1);
			const auto first = control.Complete();
			watch.OnFrameSubmitted(1);
			watch.TrackCoverageFrame(range, 1);
			watch.PrepareFrame(2);
			const auto last = control.Complete(true);
			watch.OnFrameSubmitted(2);
			watch.TrackCoverageFrame(range, 2);
			watch.CloseCoverage(range);
			context.Check(!watch.GetCoverage(range)->m_Complete && watch.GetCoverage(range)->m_FrameCount == 2,
				"Closing a submitted range waits for the complete asynchronous tail");
			watch.OnProductionFrame({ .m_FrameSerial = 2, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None } });
			context.Check(watch.OnScanResult(first) && !watch.GetCoverage(range)->m_Complete,
				"A latest counter frame and an earlier boundary do not imply complete range coverage");
			watch.OnProductionFrame({ .m_FrameSerial = 1, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None } });
			context.Check(watch.OnScanResult(last) && watch.GetCoverage(range)->m_Complete && watch.GetCoverage(range)->Fails() &&
				watch.GetCoverage(range)->m_CompletedFrames == 2 && watch.GetCoverage(range)->m_Failure.find("frame 2") != std::string::npos &&
				watch.GetCoverage(range)->m_Failure.find("channel 1") != std::string::npos,
				"A boundary anomaly after the last sequence submission fails with its frame and exact location");
			watch.ReleaseCoverage(range);
			context.Check(!watch.GetCoverage(range), "Finished consumers release their retained range state");

			context.Check(watch.Configure(configuration, error), "Missing-result range reconfigures");
			const auto missing = watch.OpenCoverage();
			watch.PrepareFrame(3);
			watch.OnFrameSubmitted(3);
			watch.TrackCoverageFrame(missing, 3);
			watch.CloseCoverage(missing);
			for (uint64_t serial = 4; serial <= 3 + NonFiniteWatchCoordinator::MaxPendingCoverageSubmissions; ++serial)
			{
				watch.OnFrameSubmitted(serial);
			}
			context.Check(watch.GetCoverage(missing)->m_Complete && watch.GetCoverage(missing)->Fails() &&
				watch.GetCoverage(missing)->m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete &&
				watch.GetCoverage(missing)->m_Production.m_Status == NonFiniteClaimStatus::Incomplete,
				"Missing results expire as Incomplete after a bounded number of successful submissions");
			watch.ReleaseCoverage(missing);
			const auto changed = watch.OpenCoverage();
			context.Check(watch.Configure({ .m_Enabled = false }, error) && watch.GetCoverage(changed)->m_Complete &&
				watch.GetCoverage(changed)->Fails(), "Disabling or replacing watch cannot certify an earlier coverage range");
			watch.ReleaseCoverage(changed);

			configuration.m_Production = false;
			control.m_Instrumentation = false;
			context.Check(watch.Configure(configuration, error), "Boundary-only fail mode is available without counter variants");
			const auto aborted = watch.OpenCoverage();
			watch.PrepareFrame(100);
			const auto unsubmitted = control.Complete(true);
			watch.PrepareFrame(101);
			watch.OnFrameSubmitted(101);
			watch.TrackCoverageFrame(aborted, 101);
			watch.CloseCoverage(aborted);
			context.Check(watch.OnScanResult(unsubmitted) && watch.OnScanResult(control.Complete()) &&
				watch.GetCoverage(aborted)->m_Complete && !watch.GetCoverage(aborted)->Fails() &&
				watch.GetCoverage(aborted)->m_FrameCount == 1 && watch.GetCoverage(aborted)->m_FirstFrameSerial == 101,
				"An aborted serial is excluded from coverage even if a synthetic late result arrives");
			watch.ReleaseCoverage(aborted);
			configuration.m_Mode = NonFiniteWatchMode::Report;
			configuration.m_Production = true;
			configuration.m_Boundary = false;
			configuration.m_Targets.clear();
			context.Check(watch.Configure(configuration, error), "Unavailable production can still be inspected in report mode");
			const auto unavailable = watch.OpenCoverage();
			watch.OnFrameSubmitted(102);
			watch.TrackCoverageFrame(unavailable, 102);
			watch.CloseCoverage(unavailable);
			context.Check(watch.GetCoverage(unavailable)->m_Complete && !watch.GetCoverage(unavailable)->Fails() &&
				watch.GetCoverage(unavailable)->m_Production.m_Status == NonFiniteClaimStatus::Incomplete,
				"Report mode completes unavailable production as Incomplete without turning it into a clean claim");
			watch.ReleaseCoverage(unavailable);

			configuration = { .m_Mode = NonFiniteWatchMode::Fail, .m_Production = false };
			context.Check(watch.Configure(configuration, error), "Missing default coverage configures");
			NonFiniteWatchDefaults defaults;
			for (size_t i = 0; i < defaults.size(); ++i)
			{
				defaults[i].m_Semantic = static_cast<NonFiniteWatchSemantic>(i);
				defaults[i].m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			defaults[0].m_Claim = {};
			defaults[0].m_Failure = "MissingCoverage: an active motion role was not bound.";
			defaults[3].m_Ready = true;
			defaults[3].m_Address.m_Resource = "SceneColor";
			const auto absent = watch.OpenCoverage();
			watch.PrepareFrame(103, false, defaults);
			watch.OnFrameSubmitted(103);
			watch.TrackCoverageFrame(absent, 103);
			watch.CloseCoverage(absent);
			context.Check(watch.OnScanResult(control.Complete()) && watch.GetCoverage(absent)->m_Complete && watch.GetCoverage(absent)->Fails() &&
				watch.GetCoverage(absent)->m_Failure.find("motion-vectors") != std::string::npos,
				"Inactive defaults with empty addresses cannot overwrite an unregistered active role's missing coverage");
			watch.ReleaseCoverage(absent);
			context.Check(watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Production = false,
				.m_Targets = { NonFiniteScanTarget{ .m_Address = { .m_Resource = "Motion" } } } }, error), "Deferred-frame watch configures");
			watch.PrepareFrame(104);
			auto deferred = control.Complete();
			deferred.m_Status = FrameCaptureStatus::Busy;
			context.Check(control.m_DeferFrame && watch.OnScanResult(deferred) && watch.GetFailModeFailure().empty(),
				"An admission-deferred unsubmitted frame contributes no missing-coverage failure history");
		}

		void RunCoverageConsumerTests(SelfTestContext& context)
		{
			WatchCaptureControl control;
			control.m_Instrumentation = true;
			NonFiniteWatchCoordinator watch(&control);
			std::string error;
			context.Check(watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Boundary = false }, error),
				"Production-only fail mode configures for consumer lifecycle checks");
			FrameCaptureCoordinator capture({ .m_Capture = &control, .m_WriteOnCallingThread = true, .m_NonFiniteWatch = &watch });
			const auto id = capture.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} } }).m_JobId;
			capture.BeginFrame({ .m_FrameSerial = 1 });
			watch.OnFrameSubmitted(1);
			capture.OnFrameSubmitted();
			control.m_Results.push_back({ .m_RequestId = control.m_CaptureId, .m_Source = FrameCaptureSource::Scene,
				.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = 1, .m_Image = std::make_shared<FrameCaptureImage>() });
			capture.Update();
			std::optional<CaptureJobResult> result;
			result = capture.TakeJobResult(id);
			context.Check(!result && capture.GetUnfinishedRequestCount() == 1,
				"A completed image does not publish before its watch result arrives");
			NonFiniteProductionFrame production{ .m_FrameSerial = 1,
				.m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced },
				.m_Sites = { { .m_ProgramIndex = 2, .m_SiteIndex = 3,
					.m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced } } } };
			control.m_Production.push_back(std::make_shared<NonFiniteProductionFrame>(production));
			capture.Update();
			result = capture.TakeJobResult(id);
			context.Check(result.has_value() && result->m_JobId == id && result->m_Status == CaptureJobStatus::Failed &&
				result->m_Products.front().m_Files.empty() && result->m_Products.front().m_Metadata->m_WatchCoverage->Fails() &&
				result->m_Products.front().m_Failure.find("NonFiniteReplaced: frame 1, program 2, site 3") != std::string::npos,
				"A sanitized production anomaly fails the waiting capture before any image publication");
			const auto pending = capture.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} },
				.m_Timing = FrameCaptureTiming::AfterReady }).m_JobId;
			capture.Update();
			result.reset();
			result = capture.TakeJobResult(pending);
			context.Check(result.has_value() && result->m_JobId == pending && result->m_Status == CaptureJobStatus::Failed,
				"Fail mode also fails an after-ready capture still waiting for readiness");

			FrameSequenceCoordinator sequence(capture);
			context.Check(watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Boundary = false }, error), "Sequence range starts with fresh diagnostic history");
			const CameraPath path{ .m_Id = "SEQ_Watch", .m_Name = "Watch", .m_Version = 1,
				.m_Keys = { { .m_Frame = 0, .m_Target = { 0, 0, 1 } },{ .m_Frame = 1, .m_Target = { 0, 0, 1 } } } };
			context.Check(sequence.Start({ .m_CameraPathId = path.m_Id }, error) != 0, "Watch sequence starts");
			for (uint64_t serial = 2; serial <= 3; ++serial)
			{
				FrameCaptureFrameState state{ .m_FrameSerial = serial };
				state.m_SettleKey.m_Temporal.m_HasCompatiblePreviousHistory = true;
				const auto pose = sequence.PrepareFrame(capture.GetLastFrameState(), std::span(&path, 1));
				sequence.OnPoseApplied(pose ? EvaluateCameraPath(path, pose->m_Frame) : std::nullopt);
				context.Check(sequence.BeginFrame(state), "The watched sequence begins its submitted frame");
				capture.BeginFrame(state);
				watch.OnFrameSubmitted(serial);
				capture.OnFrameSubmitted();
				sequence.OnFrameSubmitted(serial);
			}
			context.Check(sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"A sequence with no PNG captures still drains every submitted frame's watch evidence");
			watch.OnProductionFrame({ .m_FrameSerial = 2, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None } });
			production.m_FrameSerial = 3;
			watch.OnProductionFrame(production);
			sequence.UpdateWatchCoverage();
			context.Check(sequence.GetStatus()->m_State == FrameSequenceState::Failed && sequence.GetStatus()->m_WatchCoverage &&
				sequence.GetStatus()->m_WatchCoverage->m_FrameCount == 2 && sequence.GetStatus()->m_Failure.starts_with("NonFiniteReplaced"),
				"A late last-frame production anomaly fails the sequence even with no requested images");
			context.Check(watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Boundary = false }, error) &&
				sequence.Start({ .m_CameraPathId = path.m_Id, .m_ReferenceSamples = 3 }, error) != 0,
				"A consecutive reference sequence starts with fresh range membership");
			for (uint64_t serial = 4; serial <= 9; ++serial)
			{
				FrameCaptureFrameState state{ .m_FrameSerial = serial };
				const auto pose = sequence.PrepareFrame(capture.GetLastFrameState(), std::span(&path, 1));
				sequence.OnPoseApplied(pose ? EvaluateCameraPath(path, pose->m_Frame) : std::nullopt);
				context.Check(sequence.BeginFrame(state), "The watched reference begins its submitted sample");
				capture.BeginFrame(state);
				watch.OnFrameSubmitted(serial);
				capture.OnFrameSubmitted();
				sequence.OnFrameSubmitted(serial);
			}
			context.Check(sequence.GetStatus()->m_State == FrameSequenceState::Finishing,
				"Reference completion waits for every submitted sample rather than only its output frames");
			for (uint64_t serial = 9; serial >= 4; --serial)
			{
				watch.OnProductionFrame({ .m_FrameSerial = serial, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None } });
			}
			sequence.UpdateWatchCoverage();
			context.Check(sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
				sequence.GetStatus()->m_WatchCoverage->m_FrameCount == 6 && sequence.GetStatus()->m_WatchCoverage->m_Complete &&
				sequence.GetStatus()->m_WatchCoverage->m_Production.m_Status == NonFiniteClaimStatus::Passed,
				"A clean reference sequence completes after all six out-of-order sample results arrive");
			context.Check(sequence.Start({ .m_CameraPathId = path.m_Id }, error) != 0, "A cancellable watch range starts");
			FrameCaptureFrameState state{ .m_FrameSerial = 10 };
			const auto pose = sequence.PrepareFrame(capture.GetLastFrameState(), std::span(&path, 1));
			sequence.OnPoseApplied(pose ? EvaluateCameraPath(path, pose->m_Frame) : std::nullopt);
			context.Check(sequence.BeginFrame(state), "The cancellable watched sequence begins its frame");
			capture.BeginFrame(state);
			watch.OnFrameSubmitted(10);
			capture.OnFrameSubmitted();
			sequence.OnFrameSubmitted(10);
			sequence.UpdateWatchCoverage();
			context.Check(sequence.Cancel() == FrameSequenceCancelVerdict::Cancelled,
				"Cancelling a sequence releases its unresolved watch range");
			production.m_FrameSerial = 10;
			watch.OnProductionFrame(production);
			sequence.UpdateWatchCoverage();
			context.Check(sequence.GetStatus()->m_State == FrameSequenceState::Cancelled,
				"A cancelled sequence cannot be revived or failed by its late counter result");
			context.Check(watch.Configure({ .m_Boundary = false }, error), "Transport failure reporting configures");
			const auto transport = capture.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} } }).m_JobId;
			capture.BeginFrame({ .m_FrameSerial = 11 });
			watch.OnFrameSubmitted(11);
			capture.OnFrameSubmitted();
			control.m_Results.push_back({ .m_RequestId = control.m_CaptureId, .m_Source = FrameCaptureSource::Scene,
				.m_Status = FrameCaptureStatus::Failed, .m_FrameSerial = 11, .m_Failure = "Readback mapping failed" });
			capture.Update();
			result.reset();
			result = capture.TakeJobResult(transport);
			context.Check(result.has_value() && result->m_JobId == transport && result->m_Status == CaptureJobStatus::Failed &&
				result->m_Products.front().m_Metadata->m_WatchCoverage && result->m_Products.front().m_Metadata->m_WatchCoverage->m_Production.m_Status == NonFiniteClaimStatus::Incomplete,
				"A capture transport failure retains the unresolved watch claims in its response metadata");
			context.Check(watch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Boundary = false }, error),
				"Watch can fail an issued capture independently of image completion");
			const auto early = capture.TrySubmit({ .m_RequiredProducts = { ImageCaptureProduct{} } }).m_JobId;
			capture.BeginFrame({ .m_FrameSerial = 12 });
			watch.OnFrameSubmitted(12);
			capture.OnFrameSubmitted();
			production.m_FrameSerial = 12;
			watch.OnProductionFrame(production);
			capture.Update();
			result.reset();
			result = capture.TakeJobResult(early);
			context.Check(result.has_value() && result->m_JobId == early && result->m_Status == CaptureJobStatus::Failed &&
				result->m_Products.front().m_Failure.starts_with("NonFiniteReplaced"),
				"An issued capture fails promptly when its watch fails before any image result");
			control.m_Results.push_back({ .m_RequestId = control.m_CaptureId, .m_Source = FrameCaptureSource::Scene,
				.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = 12, .m_Image = std::make_shared<FrameCaptureImage>() });
			capture.Update();
			result.reset();
			result = capture.TakeJobResult(early);
			context.Check(!result, "Late payloads of a watch-failed capture drain without a second result or publication");
		}

		void RunInjectionTests(SelfTestContext& context)
		{
			WatchCaptureControl control;
			NonFiniteWatchCoordinator watch(&control);
			std::string error;
			const NonFiniteInjection injection{ .m_PointIndex = 0, .m_Value = NonFiniteInjectionValue::NaN,
				.m_X = 3, .m_Y = 4, .m_Component = 1 };
			NonFiniteWatchConfiguration configuration{ .m_Mode = NonFiniteWatchMode::Fail, .m_Boundary = false,
				.m_Injection = injection };
			context.Check(!watch.Configure(configuration, error) && error.starts_with("RequiresRestart") && !control.m_Injection,
				"Injection without startup instrumentation requires a restart and requests nothing");
			control.m_Instrumentation = true;
			NonFiniteWatchConfiguration boundaryOnly = configuration;
			boundaryOnly.m_Boundary = true;
			boundaryOnly.m_Production = false;
			NonFiniteWatchConfiguration badComponent = configuration;
			badComponent.m_Injection->m_Component = 4;
			context.Check(!watch.Configure(boundaryOnly, error) && !watch.Configure(badComponent, error) && !control.m_Injection,
				"Injection evidence travels with production counters and needs a valid component");
			context.Check(watch.Configure(configuration, error) && control.m_Injection == injection &&
				watch.GetStatus().m_Injection == injection,
				"An accepted watch configuration forwards its injection request to Runtime");

			const uint64_t range = watch.OpenCoverage();
			for (uint64_t frame = 1; frame <= 2; ++frame)
			{
				watch.PrepareFrame(frame);
				watch.OnFrameSubmitted(frame);
				watch.TrackCoverageFrame(range, frame);
			}
			watch.CloseCoverage(range);
			watch.OnProductionFrame({ .m_FrameSerial = 1, .m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced },
				.m_Sites = { NonFiniteProductionSite{ .m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced } } },
				.m_Injection = NonFiniteInjectionEvidence{ .m_Request = injection, .m_AppliedCount = 2 } });
			context.Check(watch.GetStatus().m_InjectionFrameSerial == 1 && watch.GetStatus().m_InjectionAppliedCount == 2 &&
				watch.GetFailModeFailure().find("under Debug injection") != std::string::npos,
				"Status and failures name the injection that produced their anomaly");
			// A frame without the configured request cannot say whether it was injected.
			watch.OnProductionFrame({ .m_FrameSerial = 2, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None } });
			context.Check(watch.GetStatus().m_Production.m_Status == NonFiniteClaimStatus::Incomplete &&
				watch.GetStatus().m_ProductionFailure.find("different injection request") != std::string::npos,
				"Production evidence without the configured injection request is incomplete");
			const auto coverage = watch.GetCoverage(range);
			context.Check(coverage && coverage->m_Complete && coverage->m_Injection == injection &&
				coverage->m_InjectedFrames == 1 && coverage->m_AppliedFrames == 1 &&
				coverage->m_Production.m_Status == NonFiniteClaimStatus::Failed &&
				coverage->m_Failure.find("under Debug injection") != std::string::npos,
				"Coverage stamps its injection with injected and applied frame counts");
			watch.ReleaseCoverage(range);
			context.Check(watch.Configure({ .m_Enabled = false }, error) && !control.m_Injection && !watch.GetStatus().m_Injection,
				"Disabling watch clears the Runtime injection request");
		}

		void RunTotalsTests(SelfTestContext& context)
		{
			WatchCaptureControl control;
			control.m_Instrumentation = true;
			NonFiniteWatchCoordinator watch(&control);
			std::string error;
			context.Check(watch.Configure({ .m_Targets = { NonFiniteScanTarget{ .m_Address = { .m_Resource = "Motion" } } } }, error),
				"Totals configuration accepts both claims");
			const auto clamp = [](uint64_t negative, float mostNegative, NonFiniteClaimStatus status)
			{
				return NonFiniteProductionSite{ .m_ProgramIndex = 1, .m_SiteIndex = 2,
					.m_Counters = { .m_NegativeClamps = negative, .m_MostNegative = mostNegative },
					.m_Claim = { status, status == NonFiniteClaimStatus::Failed ? NonFiniteWatchFailure::ClampPolicyExceeded : NonFiniteWatchFailure::None } };
			};
			watch.OnProductionFrame({ .m_FrameSerial = 3, .m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None },
				.m_Sites = { clamp(2, -0.5f, NonFiniteClaimStatus::Passed), NonFiniteProductionSite{ .m_SiteIndex = 4 } } });
			watch.OnProductionFrame({ .m_FrameSerial = 2, .m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::ClampPolicyExceeded },
				.m_Sites = { clamp(5, -3.0f, NonFiniteClaimStatus::Failed) } });
			watch.OnProductionFrame({ .m_FrameSerial = 4 });
			const auto& totals = watch.GetStatus().m_SiteTotals;
			context.Check(watch.GetStatus().m_ProductionFrames == 2 && totals.size() == 1 && totals[0].m_Frames == 2 &&
				totals[0].m_FailedFrames == 1 && totals[0].m_FirstFrameSerial == 2 && totals[0].m_Counters.m_NegativeClamps == 7 &&
				totals[0].m_Counters.m_MostNegative == -3.0f,
				"Site totals accumulate every complete frame's clamps, extremes and policy failures since configuration");

			watch.PrepareFrame(5);
			watch.OnFrameSubmitted(5);
			context.Check(watch.OnScanResult(control.Complete(true)) && watch.GetStatus().m_TargetTotals.size() == 1 &&
				watch.GetStatus().m_TargetTotals[0].m_CheckedFrames == 1 && watch.GetStatus().m_TargetTotals[0].m_FailedFrames == 1 &&
				watch.GetStatus().m_TargetTotals[0].m_FirstFailedFrameSerial == 5 && watch.GetStatus().m_TargetTotals[0].m_NanCount == 1,
				"Target totals count checked and failed boundary frames with their anomaly counts");
			context.Check(watch.Configure({ .m_Production = false, .m_Targets = { NonFiniteScanTarget{ .m_Address = { .m_Resource = "Motion" } } } }, error) &&
				watch.GetStatus().m_SiteTotals.empty() && watch.GetStatus().m_TargetTotals.empty() && watch.GetStatus().m_ProductionFrames == 0,
				"Reconfiguration resets the since-configuration totals");
		}
	}

	void RunNonFiniteWatchCoordinatorSelfTests(SelfTestContext& context) noexcept
	{
		RunCoverageTests(context);
		RunCoverageConsumerTests(context);
		RunInjectionTests(context);
		RunTotalsTests(context);
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
		context.Check(watch.Configure(configuration, error) && status.m_Mode == NonFiniteWatchMode::Fail,
			"Fail mode configures when production instrumentation is available");
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
			// A revision written this frame cannot wait for rotated spare capacity: a
			// view-dependent SkyView LUT is rewritten before a later frame could scan it.
			WatchCaptureControl writes;
			writes.m_ScanLimit = 0;
			NonFiniteWatchCoordinator writeWatch(&writes);
			context.Check(writeWatch.Configure({ .m_Mode = NonFiniteWatchMode::Fail, .m_Production = false }, error),
				"Generation fail-mode watch configures");
			NonFiniteWatchDefaults defaults;
			for (size_t i = 0; i < defaults.size(); ++i)
			{
				defaults[i].m_Semantic = static_cast<NonFiniteWatchSemantic>(i);
				defaults[i].m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			auto& cube = defaults[static_cast<size_t>(NonFiniteWatchSemantic::Environment)];
			cube.m_Ready = true;
			cube.m_Generation = { { 100, 1 }, 1 };
			cube.m_Mips.push_back({ .m_Resource = "Active.Cube", .m_Mip = 0, .m_SliceCount = 6 });
			auto& skyView = defaults[static_cast<size_t>(NonFiniteWatchSemantic::AtmosphereSkyView)];
			skyView.m_Ready = true;
			skyView.m_Generation = { { 200, 1 }, 7 };
			skyView.m_WrittenThisFrame = true;
			skyView.m_Mips.push_back({ .m_Resource = "Atmosphere.SkyView", .m_Mip = 0, .m_SliceCount = 1 });
			writeWatch.PrepareFrame(1, false, defaults);
			context.Check(writes.m_Id == 1 && writes.m_DeferFrame && writes.m_ScanTargets.size() == 1 &&
				writes.m_ScanTargets[0].m_Address.m_Resource == "Atmosphere.SkyView",
				"A generation written this frame is scanned with per-frame priority and can defer a busy frame");
			skyView.m_Generation.m_Revision = 8;
			writes.m_Busy = true;
			writeWatch.PrepareFrame(2, false, defaults);
			context.Check(writes.m_Id == 1 && writeWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"An unadmitted current-frame revision is missing coverage, never a pass");
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
			const auto range = generationWatch.OpenCoverage();
			generationWatch.OnFrameSubmitted(1);
			generationWatch.TrackCoverageFrame(range, 1);
			generationWatch.CloseCoverage(range);
			context.Check(generations.m_ScanTargets.size() == 2, "Generation work respects the shared scan target allowance");
			generationWatch.PrepareFrame(2, false, defaults);
			const auto tail = generations.Complete();
			context.Check(generations.m_ScanTargets.size() == 1 && generations.m_ScanTargets[0].m_Address.m_Mip == 2,
				"Pending generation mips are not duplicated while remaining mips advance");
			context.Check(generationWatch.OnScanResult(first) && generationWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Incomplete,
				"Partial generation completion is incomplete coverage");
			context.Check(generationWatch.OnScanResult(tail) && generationWatch.GetStatus().m_Boundary.m_Status == NonFiniteClaimStatus::Passed,
				"A generation completes only after every admitted mip has valid results");
			context.Check(generationWatch.GetCoverage(range)->m_Complete && generationWatch.GetCoverage(range)->m_FrameCount == 1 &&
				generationWatch.GetCoverage(range)->m_Boundary.m_Status == NonFiniteClaimStatus::Passed,
				"A range retains the consumed generation until its later mip scans complete");
			generationWatch.ReleaseCoverage(range);
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
