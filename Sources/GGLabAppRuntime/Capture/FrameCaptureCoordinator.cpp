#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/CaptureWriter.h"
#include "Capture/NonFiniteWatchCoordinator.h"

#include "AppRuntimeLog.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>
#include <variant>

namespace gglab
{
	// One shared SameFrame attempt. Runtime still owns GPU resources and settlement.
	struct FrameCaptureCoordinator::FrameAttempt
	{
		uint64_t m_FrameSerial = 0;
		uint64_t m_WatchCoverageId = 0;
		bool m_Submitted = false;
	};

	struct FrameCaptureCoordinator::ProductAttempt
	{
		uint64_t m_CaptureRequestId = 0;
		FrameCaptureMetadata m_Metadata{};
		std::optional<FrameCaptureResult> m_PendingResult;
	};

	struct FrameCaptureCoordinator::CaptureJob
	{
		CapturePlan m_Plan;
		std::optional<size_t> m_ReceiptSlot;
		CaptureJobResult m_Result;
		uint64_t m_AttemptCount = 0;
		uint32_t m_FinishedProducts = 0;
		// Transitional delivery identities, not inferred product membership.
		std::vector<uint64_t> m_OutputIds;
		size_t m_RemainingOutputs = 0;
		bool m_ViewApplied = false;
		std::optional<uint64_t> m_ViewCameraResetSerial;
		std::optional<FrameAttempt> m_Attempt;

		[[nodiscard]] uint64_t GetRuntimeGroupId() const noexcept
		{
			return m_Plan.m_Sequence || m_OutputIds.size() > 1 ? m_OutputIds.front() : 0;
		}
	};

	struct FrameCaptureCoordinator::JobReceipt
	{
		uint64_t m_JobId = 0;
		std::shared_ptr<CaptureJob> m_Job;
		std::optional<CaptureJobResult> m_Result;
		CaptureJobSnapshot m_FinalSnapshot;
	};

	struct FrameCaptureCoordinator::DrainingWrite
	{
		std::shared_ptr<CaptureWriteJob> m_Job;
		uint32_t m_Reservations = 0;
		bool m_AbandonedAtShutdown = false;
	};

	// Legacy output routing survives until the versioned request/result migration.
	// Product readiness, frame retry and watch coverage belong to the shared job.
	struct FrameCaptureCoordinator::CaptureOutput
	{
		uint64_t m_Id = 0;
		std::shared_ptr<CaptureJob> m_Job;
		size_t m_ProductIndex = 0;
		std::optional<size_t> m_GraphIndex;
		Phase m_Phase = Phase::Waiting;
		std::optional<ProductAttempt> m_Attempt;
		RGObservationFailure m_ResourceFailure = RGObservationFailure::None;
		std::shared_ptr<CaptureWriteJob> m_WriteJob;
		std::optional<CaptureWriteReceipt> m_WriteReceipt;

		[[nodiscard]] const CaptureProductSpec& GetProduct() const noexcept
		{
			return m_Job->m_Plan.m_RequiredProducts[m_ProductIndex];
		}

		[[nodiscard]] FrameCaptureSource GetSource() const noexcept
		{
			return GetCaptureProductSource(GetProduct());
		}

		[[nodiscard]] std::optional<PostProcessDebugTap> GetDiagnosticTap() const noexcept
		{
			const auto* diagnostic = std::get_if<DiagnosticCaptureProduct>(&GetProduct());
			return diagnostic ? diagnostic->m_Tap : std::nullopt;
		}

		[[nodiscard]] bool IsGraphSummary() const noexcept
		{
			const auto* graph = std::get_if<GraphCaptureProduct>(&GetProduct());
			return graph && graph->m_Summary;
		}
	};

	namespace
	{
		constexpr std::chrono::seconds EncodeStallReportTime{ 10 };
		constexpr size_t MaxFileStemComponentLength = 64;

		[[nodiscard]] bool HasResources(const CapturePlan& plan) noexcept
		{
			return std::ranges::any_of(plan.m_RequiredProducts, [](const auto& product)
				{
					return std::holds_alternative<ResourceCaptureProduct>(product);
				});
		}

		[[nodiscard]] uint32_t GetOutputCount(const CapturePlan& plan) noexcept
		{
			const bool resources = HasResources(plan);
			return static_cast<uint32_t>(std::ranges::count_if(plan.m_RequiredProducts,
				[resources](const auto& product)
				{
					return !resources || !std::holds_alternative<GraphCaptureProduct>(product);
				}));
		}

		[[nodiscard]] bool NeedsGroupAdmission(const CapturePlan& plan) noexcept
		{
			return GetOutputCount(plan) > 1 || (plan.m_Sequence && HasResources(plan));
		}

		[[nodiscard]] std::string DescribeFailedGates(const FrameCaptureReadiness& readiness)
		{
			std::string failure;
			for (const FrameCaptureGate& gate : readiness.m_Gates)
			{
				if (gate.m_State != FrameCaptureGateState::Failed)
				{
					continue;
				}
				failure += failure.empty() ? "Readiness failed: " : "; ";
				failure += gate.m_Detail.empty() ?
					gate.m_Name :
					std::format("{} ({})", gate.m_Name, gate.m_Detail);
			}
			if (!failure.empty())
			{
				failure += '.';
			}
			return failure;
		}

		[[nodiscard]] std::string SanitizeFileStemComponent(std::string_view value) noexcept
		{
			std::string result;
			for (const char character : value)
			{
				if (result.size() >= MaxFileStemComponentLength)
				{
					break;
				}
				const bool allowed = (character >= 'a' && character <= 'z') ||
					(character >= 'A' && character <= 'Z') ||
					(character >= '0' && character <= '9') || character == '-' ||
					character == '_' || character == '.';
				result += allowed ? character : '_';
			}
			return result.empty() ? std::string("capture") : result;
		}
	}

	FrameCaptureCoordinator::FrameCaptureCoordinator(const CreateInfo& createInfo) noexcept :
		m_Capture(createInfo.m_Capture),
		m_NonFiniteWatch(createInfo.m_NonFiniteWatch),
		m_DefaultOutputDirectory(createInfo.m_DefaultOutputDirectory),
		m_ShutdownWriteTimeout(createInfo.m_ShutdownWriteTimeout),
		m_Writer(std::make_unique<CaptureWriter>(createInfo.m_ImageEncoder,
			createInfo.m_WriteOnCallingThread, createInfo.m_PublicationFileSystem)),
		m_RunIdentity(CreateCaptureRunIdentity()),
		m_JobLimits(createInfo.m_JobLimits),
		m_JobReceipts(m_JobLimits.m_MaxReceipts)
	{
		static_assert(MaxInFlightCaptures == CaptureWriter::MaxInFlightJobs);
	}

	FrameCaptureCoordinator::~FrameCaptureCoordinator()
	{
		FinishWriting();
	}

	CaptureAdmission FrameCaptureCoordinator::TrySubmit(CapturePlan&& plan) noexcept
	{
		if (m_IsShuttingDown)
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::ShuttingDown };
		}
		if (plan.m_RequiredProducts.empty() ||
			(plan.m_Timing != FrameCaptureTiming::NextFrame && plan.m_Timing != FrameCaptureTiming::AfterReady))
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::InvalidPlan };
		}
		if (plan.m_RequiredProducts.size() > m_JobLimits.m_MaxProductsPerJob)
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::PlanTooLarge };
		}
		uint32_t resources = 0;
		uint32_t graphs = 0;
		bool graphSummary = false;
		for (const auto& product : plan.m_RequiredProducts)
		{
			if (const auto* resource = std::get_if<ResourceCaptureProduct>(&product))
			{
				++resources;
				if (resource->m_Targets.empty())
				{
					return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::InvalidPlan };
				}
				if (resource->m_Targets.size() > m_JobLimits.m_MaxResourceTargetsPerJob)
				{
					return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::PlanTooLarge };
				}
			}
			else if (const auto* graph = std::get_if<GraphCaptureProduct>(&product))
			{
				++graphs;
				graphSummary = graph->m_Summary;
			}
			else if (const auto* image = std::get_if<ImageCaptureProduct>(&product))
			{
				if (image->m_Source != CaptureImageSource::Scene && image->m_Source != CaptureImageSource::Composited)
				{
					return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::InvalidPlan };
				}
			}
			else if (const auto* diagnostic = std::get_if<DiagnosticCaptureProduct>(&product))
			{
				if (!diagnostic->m_Tap)
				{
					return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::InvalidPlan };
				}
			}
			else
			{
				return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::InvalidPlan };
			}
		}
		if (resources > 1 || graphs > 1 || (resources != 0 && graphs != 0 && graphSummary))
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::InvalidPlan };
		}
		if (m_NextJobId == 0)
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::IdentityExhausted };
		}
		if (m_RunIdentity.empty())
		{
			return { CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::IdentityUnavailable };
		}
		if (m_ActiveJobs >= m_JobLimits.m_MaxActiveJobs)
		{
			return { CaptureAdmissionStatus::Busy, CaptureAdmissionReason::ActiveJobLimit };
		}
		const auto slot = std::ranges::find(m_JobReceipts, uint64_t{ 0 }, &JobReceipt::m_JobId);
		if (slot == m_JobReceipts.end())
		{
			return { CaptureAdmissionStatus::Busy, CaptureAdmissionReason::ReceiptLimit };
		}
		RemoveFinishedJobs();
		slot->m_JobId = m_NextJobId++;
		++m_ActiveJobs;
		slot->m_Job = EnqueuePlan(std::move(plan),
			static_cast<size_t>(std::distance(m_JobReceipts.begin(), slot)), slot->m_JobId);
		return { CaptureAdmissionStatus::Accepted, CaptureAdmissionReason::None, slot->m_JobId };
	}

	std::optional<CaptureJobResult> FrameCaptureCoordinator::TakeJobResult(uint64_t jobId) noexcept
	{
		if (jobId == 0)
		{
			return std::nullopt;
		}
		const auto slot = std::ranges::find(m_JobReceipts, jobId, &JobReceipt::m_JobId);
		if (slot == m_JobReceipts.end() || !slot->m_Result)
		{
			return std::nullopt;
		}
		auto result = std::move(slot->m_Result);
		*slot = {};
		RemoveFinishedJobs();
		return result;
	}

	CaptureIntakeSnapshot FrameCaptureCoordinator::GetIntakeSnapshot() const noexcept
	{
		CaptureIntakeSnapshot snapshot{ .m_Limits = m_JobLimits, .m_ActiveJobs = m_ActiveJobs,
			.m_DrainingRuntimeRequests = static_cast<uint32_t>(m_DrainingCaptureRequests.size()),
			.m_DrainingWriterJobs = static_cast<uint32_t>(m_DrainingWrites.size()),
			.m_LegacyRetainedResults = m_Results.size() };
		for (const DrainingWrite& write : m_DrainingWrites)
		{
			snapshot.m_DrainingWriteReservations += write.m_Reservations;
		}
		for (const JobReceipt& slot : m_JobReceipts)
		{
			snapshot.m_ReservedReceipts += slot.m_JobId != 0;
			snapshot.m_TerminalReceipts += slot.m_Result.has_value();
		}
		for (const CaptureOutput& output : m_Outputs)
		{
			snapshot.m_LegacyUnfinishedOutputs += !output.m_Job->m_ReceiptSlot && output.m_Phase != Phase::Finished;
		}
		return snapshot;
	}

	std::optional<CaptureJobSnapshot> FrameCaptureCoordinator::GetJobSnapshot(uint64_t jobId) const noexcept
	{
		if (jobId == 0)
		{
			return std::nullopt;
		}
		const auto slot = std::ranges::find(m_JobReceipts, jobId, &JobReceipt::m_JobId);
		if (slot == m_JobReceipts.end())
		{
			return std::nullopt;
		}
		if (slot->m_Result)
		{
			return slot->m_FinalSnapshot;
		}
		const CaptureJob& job = *slot->m_Job;
		CaptureJobSnapshot snapshot{ .m_JobId = jobId, .m_AttemptCount = job.m_AttemptCount,
			.m_RequiredProducts = static_cast<uint32_t>(job.m_Plan.m_RequiredProducts.size()),
			.m_FinishedProducts = job.m_FinishedProducts };
		if (job.m_Attempt)
		{
			snapshot.m_FrameSerial = job.m_Attempt->m_FrameSerial;
		}
		Phase first = Phase::Finished;
		for (const CaptureOutput& output : m_Outputs)
		{
			if (output.m_Job.get() == &job)
			{
				first = std::min(first, output.m_Phase);
			}
		}
		switch (first)
		{
		case Phase::Waiting:
			snapshot.m_Stage = CaptureJobStage::Waiting;
			break;
		case Phase::Issued:
			snapshot.m_Stage = CaptureJobStage::AwaitingProducts;
			break;
		case Phase::AwaitingWatch:
			snapshot.m_Stage = CaptureJobStage::AwaitingCoverage;
			break;
		case Phase::Encoding:
			snapshot.m_Stage = CaptureJobStage::Encoding;
			break;
		case Phase::Finished:
			snapshot.m_Stage = CaptureJobStage::Finished;
			break;
		}
		return snapshot;
	}

	CaptureCancelResult FrameCaptureCoordinator::CancelJob(uint64_t jobId) noexcept
	{
		if (jobId == 0)
		{
			return CaptureCancelResult::NotFound;
		}
		const auto slot = std::ranges::find(m_JobReceipts, jobId, &JobReceipt::m_JobId);
		if (slot == m_JobReceipts.end())
		{
			return CaptureCancelResult::NotFound;
		}
		if (slot->m_Result)
		{
			return CaptureCancelResult::Finished;
		}
		// Keep the job alive when Finish releases its receipt's active reference.
		const auto job = slot->m_Job;
		const auto outputs = GetOutputs(*job);
		GGLAB_ASSERT_MSG(!outputs.empty(), "An active job retains its required outputs.");
		if (outputs.front()->m_Phase == Phase::Encoding)
		{
			if (TryFinishWritten(*outputs.front()))
			{
				return CaptureCancelResult::Finished;
			}
			const auto write = outputs.front()->m_WriteJob;
			if (!write->TryCancel())
			{
				return TryFinishWritten(*outputs.front()) ?
					CaptureCancelResult::Finished : CaptureCancelResult::TooLate;
			}
			// The cancelled receipt may be consumed before this worker unwinds.
			// Retain every reservation independently until its real receipt arrives.
			m_DrainingWrites.push_back({ write, static_cast<uint32_t>(outputs.size()) });
		}
		else if (job->m_Attempt && !job->m_Attempt->m_Submitted)
		{
			// A graph may still borrow provisional resources. Request the normal
			// pre-submit RAII abort; never destroy Runtime owners from CancelJob.
			m_AbortFrameSerial = job->m_Attempt->m_FrameSerial;
		}
		for (CaptureOutput* member : outputs)
		{
			if (member->m_Phase == Phase::Issued && member->m_Attempt->m_CaptureRequestId != 0)
			{
				m_DrainingCaptureRequests.push_back(member->m_Attempt->m_CaptureRequestId);
			}
			Finish(*member, FrameCaptureRequestStatus::Cancelled, "Cancelled by request.");
		}
		return CaptureCancelResult::Cancelled;
	}

	CaptureWaitingCancelResult FrameCaptureCoordinator::CancelWaitingJob(uint64_t jobId) noexcept
	{
		if (jobId == 0)
		{
			return CaptureWaitingCancelResult::NotFound;
		}
		const auto slot = std::ranges::find(m_JobReceipts, jobId, &JobReceipt::m_JobId);
		if (slot == m_JobReceipts.end())
		{
			return CaptureWaitingCancelResult::NotFound;
		}
		if (slot->m_Result)
		{
			return CaptureWaitingCancelResult::Finished;
		}
		return Cancel(slot->m_Job->m_OutputIds.front()) ?
			CaptureWaitingCancelResult::Cancelled :
			CaptureWaitingCancelResult::TooLate;
	}

	CapturePlan FrameCaptureCoordinator::MakeCapturePlan(FrameCaptureRequest request) noexcept
	{
		CapturePlan plan{
			.m_Timing = request.m_Timing,
			.m_SettleFrames = request.m_SettleFrames,
			.m_RequiredContentId = std::move(request.m_RequiredContentId),
			.m_ReferenceViewId = std::move(request.m_ReferenceViewId),
			.m_WaitForCapacity = request.m_WaitForCapacity,
			.m_OutputDirectory = std::move(request.m_OutputDirectory),
			.m_Label = std::move(request.m_Label),
			.m_Note = std::move(request.m_Note),
			.m_Sequence = std::move(request.m_Sequence),
		};
		switch (request.m_Source)
		{
		case FrameCaptureSource::Scene:
			plan.m_RequiredProducts.emplace_back(ImageCaptureProduct{ CaptureImageSource::Scene });
			break;
		case FrameCaptureSource::Composited:
			plan.m_RequiredProducts.emplace_back(ImageCaptureProduct{ CaptureImageSource::Composited });
			break;
		case FrameCaptureSource::Diagnostic:
			plan.m_RequiredProducts.emplace_back(DiagnosticCaptureProduct{ request.m_DiagnosticTap });
			break;
		case FrameCaptureSource::RenderGraph:
			plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{ request.m_GraphSummary });
			break;
		case FrameCaptureSource::ResourceExport:
			plan.m_RequiredProducts.emplace_back(ResourceCaptureProduct{ std::move(request.m_ResourceTargets) });
			if (request.m_IncludeGraph)
			{
				plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{});
			}
			break;
		default:
			plan.m_RequiredProducts.emplace_back(UnsupportedCaptureProduct{ request.m_Source });
			break;
		}
		return plan;
	}

	uint64_t FrameCaptureCoordinator::Submit(FrameCaptureRequest request) noexcept
	{
		return SubmitPlan(MakeCapturePlan(std::move(request))).front();
	}

	std::vector<uint64_t> FrameCaptureCoordinator::SubmitPlan(CapturePlan plan) noexcept
	{
		return EnqueuePlan(std::move(plan))->m_OutputIds;
	}

	std::shared_ptr<FrameCaptureCoordinator::CaptureJob> FrameCaptureCoordinator::EnqueuePlan(
		CapturePlan plan, std::optional<size_t> receiptSlot, uint64_t jobId) noexcept
	{
		GGLAB_ASSERT_MSG(!plan.m_RequiredProducts.empty(),
			"An internal capture plan must require a product.");
		const bool resources = HasResources(plan);
		std::optional<size_t> graphIndex;
		size_t resourceCount = 0;
		for (size_t index = 0; index < plan.m_RequiredProducts.size(); ++index)
		{
			const auto& product = plan.m_RequiredProducts[index];
			resourceCount += std::holds_alternative<ResourceCaptureProduct>(product);
			if (const auto* graph = std::get_if<GraphCaptureProduct>(&product))
			{
				GGLAB_ASSERT_MSG(!graphIndex, "An internal plan permits one graph product.");
				GGLAB_ASSERT_MSG(!resources || !graph->m_Summary,
					"A resource delivery requires the existing full graph format.");
				graphIndex = index;
			}
		}
		GGLAB_ASSERT_MSG(resourceCount <= 1, "An internal plan permits one resource product.");

		auto job = std::make_shared<CaptureJob>();
		job->m_ReceiptSlot = receiptSlot;
		if (receiptSlot)
		{
			job->m_Result.m_JobId = jobId;
			job->m_Result.m_Products.resize(plan.m_RequiredProducts.size());
		}
		job->m_RemainingOutputs = GetOutputCount(plan);
		job->m_Plan = std::move(plan);
		for (size_t index = 0; index < job->m_RemainingOutputs; ++index)
		{
			job->m_OutputIds.push_back(m_NextRequestId++);
		}
		size_t outputIndex = 0;
		for (size_t index = 0; index < job->m_Plan.m_RequiredProducts.size(); ++index)
		{
			const auto& product = job->m_Plan.m_RequiredProducts[index];
			if (resources && std::holds_alternative<GraphCaptureProduct>(product))
			{
				continue;
			}
			CaptureOutput& entry = m_Outputs.emplace_back();
			entry.m_Id = job->m_OutputIds[outputIndex++];
			entry.m_Job = job;
			entry.m_ProductIndex = index;
			if (std::holds_alternative<ResourceCaptureProduct>(product))
			{
				entry.m_GraphIndex = graphIndex;
			}
		}
		if (m_IsShuttingDown)
		{
			for (CaptureOutput* entry : GetOutputs(*job))
			{
				Finish(*entry, FrameCaptureRequestStatus::Cancelled, "The runtime is shutting down.");
			}
		}
		return job;
	}

	std::vector<FrameCaptureCoordinator::CaptureOutput*> FrameCaptureCoordinator::GetOutputs(
		const CaptureJob& job) noexcept
	{
		std::vector<CaptureOutput*> outputs;
		for (CaptureOutput& entry : m_Outputs)
		{
			if (entry.m_Job.get() == &job && entry.m_Phase != Phase::Finished)
			{
				outputs.push_back(&entry);
			}
		}
		return outputs;
	}

	bool FrameCaptureCoordinator::Cancel(uint64_t requestId) noexcept
	{
		const auto entry = std::ranges::find(m_Outputs, requestId, &CaptureOutput::m_Id);
		if (entry == m_Outputs.end() || entry->m_Phase != Phase::Waiting)
		{
			return false;
		}
		const auto outputs = GetOutputs(*entry->m_Job);
		if (!std::ranges::all_of(outputs, [](const auto* member) { return member->m_Phase == Phase::Waiting; }))
		{
			return false;
		}
		for (CaptureOutput* member : outputs)
		{
			Finish(*member, FrameCaptureRequestStatus::Cancelled, "Cancelled by request.");
		}
		return true;
	}

	void FrameCaptureCoordinator::ConsumeResults(
		std::vector<FrameCaptureRequestResult>& outResults) noexcept
	{
		RemoveFinishedJobs();
		outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
			std::make_move_iterator(m_Results.end()));
		m_Results.clear();
	}

	uint32_t FrameCaptureCoordinator::GetUnfinishedRequestCount() const noexcept
	{
		return static_cast<uint32_t>(std::ranges::count_if(m_Outputs,
			[](const CaptureOutput& entry) { return entry.m_Phase != Phase::Finished; }));
	}

	const FrameCaptureFrameState* FrameCaptureCoordinator::GetLastFrameState() const noexcept
	{
		return m_LastFrameState ? &*m_LastFrameState : nullptr;
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(
		FrameCaptureSource source, uint32_t requestCount) const noexcept
	{
		return m_LastFrameState ?
			GetCaptureAvailability(source, *m_LastFrameState, requestCount) :
			GetCaptureAvailability(source, FrameCaptureFrameState{}, requestCount);
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(
		const CapturePlan& plan) const noexcept
	{
		return m_LastFrameState ?
			GetCaptureAvailability(plan, *m_LastFrameState) :
			GetCaptureAvailability(plan, FrameCaptureFrameState{});
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(FrameCaptureSource source,
		const FrameCaptureFrameState& state, uint32_t requestCount) const noexcept
	{
		if (!m_Capture || m_IsShuttingDown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		// Reservations cover GPU issue through writer completion, not just queued jobs.
		auto reserved = std::ranges::count_if(m_Outputs, [](const CaptureOutput& entry)
			{
				return entry.m_Phase == Phase::Issued || entry.m_Phase == Phase::AwaitingWatch ||
					entry.m_Phase == Phase::Encoding;
			});
		reserved += m_DrainingCaptureRequests.size();
		for (const DrainingWrite& write : m_DrainingWrites)
		{
			reserved += write.m_Reservations;
		}
		if (requestCount > MaxInFlightCaptures)
		{
			return FrameCaptureAvailability::TooLarge;
		}
		if (reserved > MaxInFlightCaptures - requestCount)
		{
			return FrameCaptureAvailability::Busy;
		}
		const auto requests = m_Capture->GetRequestAvailability(requestCount);
		if (requests != FrameCaptureAvailability::Available)
		{
			return requests;
		}
		if (source == FrameCaptureSource::RenderGraph || source == FrameCaptureSource::ResourceExport)
		{
			return source == FrameCaptureSource::ResourceExport ?
				m_Capture->GetResourceExportFrameAvailability() :
				requests;
		}
		if (state.m_FrameSerial == 0)
		{
			return FrameCaptureAvailability::Available;
		}
		const ViewExtent extent = state.m_SettleKey.m_Temporal.m_Identity.m_ColorExtent;
		return m_Capture->GetAvailability(source == FrameCaptureSource::Diagnostic ?
			RHIFormat::R8G8B8A8Unorm :
			state.m_DisplayFormat,
			extent.m_Width, extent.m_Height);
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(
		const CapturePlan& plan, const FrameCaptureFrameState& state) const noexcept
	{
		const uint32_t count = GetOutputCount(plan);
		FrameCaptureAvailability availability = FrameCaptureAvailability::Available;
		for (const auto& product : plan.m_RequiredProducts)
		{
			const auto current = GetCaptureAvailability(GetCaptureProductSource(product), state, count);
			// A temporary blocker must not hide an impossible required product.
			if (current != FrameCaptureAvailability::Available && current != FrameCaptureAvailability::Busy)
			{
				return current;
			}
			if (current == FrameCaptureAvailability::Busy)
			{
				availability = current;
			}
		}
		return availability;
	}

	void FrameCaptureCoordinator::BeginFrame(FrameCaptureFrameState state) noexcept
	{
		const bool ready = state.m_Readiness.IsReady();
		const bool historyRestarted =
			!m_LastFrameState || !state.m_SettleKey.Continues(m_LastFrameState->m_SettleKey);
		m_HistoryReady = historyRestarted ? ready : m_HistoryReady && ready;
		if (!m_HistoryReady || historyRestarted)
		{
			m_SettledFrames = 0;
		}
		for (CaptureOutput& entry : m_Outputs)
		{
			CaptureJob& job = *entry.m_Job;
			if (entry.m_Phase != Phase::Waiting || !job.m_ViewApplied)
			{
				continue;
			}
			if (!job.m_ViewCameraResetSerial)
			{
				job.m_ViewCameraResetSerial = state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity;
			}
			else if (*job.m_ViewCameraResetSerial != state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity)
			{
				job.m_ViewApplied = false;
				job.m_ViewCameraResetSerial.reset();
			}
		}
		m_HasOpenFrame = true;
		m_AbortFrameSerial.reset();
		if (!m_IsShuttingDown)
		{
			std::optional<PostProcessDebugTap> diagnosticTap =
				m_Capture ? m_Capture->GetPendingDiagnosticTap() : std::nullopt;
			const std::string readinessFailure = DescribeFailedGates(state.m_Readiness);
			IssueRequiredPlans(state, diagnosticTap);
			for (CaptureOutput& entry : m_Outputs)
			{
				CaptureJob& job = *entry.m_Job;
				const CapturePlan& plan = job.m_Plan;
				if (entry.m_Phase != Phase::Waiting || NeedsGroupAdmission(plan))
				{
					continue;
				}
				if (!readinessFailure.empty() && plan.m_Timing == FrameCaptureTiming::AfterReady &&
					MatchesRequiredContent(job, state))
				{
					FailPlan(job, readinessFailure);
				}
				else if (IsDue(job, state))
				{
					const auto tap = entry.GetDiagnosticTap();
					if (entry.GetSource() == FrameCaptureSource::Diagnostic && tap &&
						!GetFrameCaptureDiagnosticTapName(*tap).empty() && diagnosticTap && diagnosticTap != tap)
					{
						if (plan.m_Sequence)
						{
							FailPlan(job, "A different diagnostic tap occupies the requested sequence frame.");
						}
						continue;
					}
					Issue(entry, state);
					if (entry.m_Phase == Phase::Issued && entry.GetSource() == FrameCaptureSource::Diagnostic)
					{
						diagnosticTap = tap;
					}
				}
			}
		}
		m_LastFrameState = std::move(state);
	}

	void FrameCaptureCoordinator::IssueRequiredPlans(const FrameCaptureFrameState& state,
		std::optional<PostProcessDebugTap>& diagnosticTap) noexcept
	{
		const std::string readinessFailure = DescribeFailedGates(state.m_Readiness);
		std::vector<CaptureJob*> visited;
		for (CaptureOutput& entry : m_Outputs)
		{
			CaptureJob& job = *entry.m_Job;
			const CapturePlan& plan = job.m_Plan;
			if (entry.m_Phase != Phase::Waiting || !NeedsGroupAdmission(plan) ||
				std::ranges::find(visited, &job) != visited.end())
			{
				continue;
			}
			visited.push_back(&job);
			if (!readinessFailure.empty() && plan.m_Timing == FrameCaptureTiming::AfterReady &&
				MatchesRequiredContent(job, state))
			{
				FailPlan(job, readinessFailure);
				continue;
			}
			if (!IsDue(job, state))
			{
				continue;
			}
			const auto outputs = GetOutputs(job);
			GGLAB_ASSERT_MSG(outputs.size() == job.m_OutputIds.size() &&
				std::ranges::all_of(outputs, [](const auto* output) { return output->m_Phase == Phase::Waiting; }),
				"A required-product plan must enter admission as a whole.");
			std::string failure;
			std::optional<PostProcessDebugTap> planTap = diagnosticTap;
			for (const auto* member : outputs)
			{
				if (member->GetSource() != FrameCaptureSource::Diagnostic)
				{
					continue;
				}
				const auto tap = member->GetDiagnosticTap();
				if (!tap || GetFrameCaptureDiagnosticTapName(*tap).empty())
				{
					failure = "A diagnostic capture requires a supported diagnostic tap.";
					break;
				}
				if (planTap && planTap != tap)
				{
					failure = "A different diagnostic tap occupies the requested same-frame plan.";
					break;
				}
				planTap = tap;
			}
			const auto availability = GetCaptureAvailability(plan, state);
			if (failure.empty() && availability == FrameCaptureAvailability::Busy)
			{
				continue;
			}
			if (!failure.empty() || availability != FrameCaptureAvailability::Available)
			{
				if (failure.empty())
				{
					failure = availability == FrameCaptureAvailability::TooLarge ?
						"The same-frame capture plan exceeds the capture capacity budget." :
						"The same-frame capture plan is unavailable.";
				}
				for (CaptureOutput* member : outputs)
				{
					if (member->GetSource() == FrameCaptureSource::ResourceExport &&
						availability == FrameCaptureAvailability::TooLarge)
					{
						member->m_ResourceFailure = RGObservationFailure::TooLarge;
					}
				}
				FailPlan(job, std::move(failure));
				continue;
			}
			// Advisory checks covered every required output. Issue contiguously on
			// the frame thread; an unexpected rejection aborts accepted siblings.
			for (CaptureOutput* member : outputs)
			{
				Issue(*member, state, true);
				if (member->m_Phase != Phase::Issued)
				{
					break;
				}
			}
			if (job.m_Attempt && m_AbortFrameSerial != state.m_FrameSerial)
			{
				diagnosticTap = planTap;
			}
		}
	}

	std::optional<FrameCaptureViewChange> FrameCaptureCoordinator::GetPendingViewChange() const noexcept
	{
		if (m_IsShuttingDown || !m_LastFrameState)
		{
			return std::nullopt;
		}
		const auto head = std::ranges::find(m_Outputs, Phase::Waiting, &CaptureOutput::m_Phase);
		if (head == m_Outputs.end())
		{
			return std::nullopt;
		}
		const CaptureJob& job = *head->m_Job;
		const CapturePlan& plan = job.m_Plan;
		if (plan.m_ReferenceViewId.empty() || job.m_ViewApplied ||
			!MatchesRequiredContent(job, *m_LastFrameState))
		{
			return std::nullopt;
		}
		if (plan.m_Timing == FrameCaptureTiming::AfterReady && !m_LastFrameState->m_Readiness.IsReady())
		{
			return std::nullopt;
		}
		if (plan.m_WaitForCapacity && !plan.m_Sequence &&
			GetCaptureAvailability(plan, *m_LastFrameState) == FrameCaptureAvailability::Busy)
		{
			return std::nullopt;
		}
		return FrameCaptureViewChange{ head->m_Id, plan.m_ReferenceViewId };
	}

	bool FrameCaptureCoordinator::ShouldRestartTemporalHistory() const noexcept
	{
		if (m_IsShuttingDown || !m_LastFrameState || m_HistoryReady ||
			!m_LastFrameState->m_Readiness.IsReady())
		{
			return false;
		}
		return std::ranges::any_of(m_Outputs, [this](const CaptureOutput& entry)
			{
				const CaptureJob& job = *entry.m_Job;
				const bool viewSettling = job.m_Plan.m_ReferenceViewId.empty() ||
					job.m_ViewCameraResetSerial.has_value();
				return entry.m_Phase == Phase::Waiting &&
					job.m_Plan.m_Timing == FrameCaptureTiming::AfterReady &&
					viewSettling && MatchesRequiredContent(job, *m_LastFrameState);
			});
	}

	bool FrameCaptureCoordinator::ShouldHoldTime() const noexcept
	{
		if (m_IsShuttingDown || m_HistoryReady)
		{
			return false;
		}
		return std::ranges::any_of(m_Outputs, [](const CaptureOutput& entry)
			{
				return entry.m_Phase == Phase::Waiting &&
					entry.m_Job->m_Plan.m_Timing == FrameCaptureTiming::AfterReady;
			});
	}

	void FrameCaptureCoordinator::OnReferenceViewApplied(uint64_t requestId, bool restored) noexcept
	{
		const auto entry = std::ranges::find_if(m_Outputs, [&](const CaptureOutput& candidate)
			{
				return candidate.m_Id == requestId && candidate.m_Phase == Phase::Waiting;
			});
		if (entry == m_Outputs.end())
		{
			return;
		}
		CaptureJob& job = *entry->m_Job;
		if (restored)
		{
			job.m_ViewApplied = true;
			job.m_ViewCameraResetSerial.reset();
			return;
		}
		std::string available;
		std::string content;
		if (m_LastFrameState)
		{
			for (const std::string& id : m_LastFrameState->m_ReferenceViewIds)
			{
				available += available.empty() ? id : ", " + id;
			}
			content = !m_LastFrameState->m_LabId.empty() ?
				m_LastFrameState->m_LabId :
				m_LastFrameState->m_DemoId;
		}
		FailPlan(job, std::format("Reference view '{}' is not registered by '{}' (available: {}).",
			job.m_Plan.m_ReferenceViewId, content, available.empty() ? "none" : available));
	}

	void FrameCaptureCoordinator::SetFrameSceneInputs(
		uint64_t frameSerial, const FrameCaptureSceneInputs& inputs) noexcept
	{
		for (CaptureOutput& entry : m_Outputs)
		{
			if (entry.m_Phase == Phase::Issued && entry.m_Attempt->m_Metadata.m_FrameSerial == frameSerial)
			{
				entry.m_Attempt->m_Metadata.m_SceneInputs = inputs;
			}
		}
	}

	void FrameCaptureCoordinator::OnFrameSubmitted() noexcept
	{
		if (m_LastFrameState)
		{
			for (CaptureOutput& entry : m_Outputs)
			{
				auto& attempt = entry.m_Job->m_Attempt;
				if (attempt && !attempt->m_Submitted &&
					attempt->m_FrameSerial == m_LastFrameState->m_FrameSerial)
				{
					attempt->m_Submitted = true;
					if (m_NonFiniteWatch && attempt->m_WatchCoverageId != 0)
					{
						m_NonFiniteWatch->TrackCoverageFrame(attempt->m_WatchCoverageId, attempt->m_FrameSerial);
						m_NonFiniteWatch->CloseCoverage(attempt->m_WatchCoverageId);
					}
				}
			}
		}
		if (m_HasOpenFrame && m_HistoryReady && m_SettledFrames < std::numeric_limits<uint32_t>::max())
		{
			++m_SettledFrames;
		}
		m_HasOpenFrame = false;
	}

	void FrameCaptureCoordinator::Update() noexcept
	{
		if (m_NonFiniteWatch)
		{
			m_NonFiniteWatch->UpdateCoverage();
		}
		if (m_Capture)
		{
			std::vector<FrameCaptureResult> results;
			m_Capture->ConsumeResults(results);
			for (FrameCaptureResult& result : results)
			{
				if (!m_NonFiniteWatch || !m_NonFiniteWatch->OnScanResult(result))
				{
					HandleCaptureResult(std::move(result));
				}
			}
			std::vector<std::shared_ptr<const NonFiniteProductionFrame>> production;
			m_Capture->ConsumeNonFiniteProductionFrames(production);
			if (m_NonFiniteWatch)
			{
				for (const auto& frame : production)
				{
					m_NonFiniteWatch->OnProductionFrame(*frame);
				}
			}
		}
		PollWatchCaptures();
		CollectWrittenJobs();
	}

	void FrameCaptureCoordinator::PrepareForShutdown() noexcept
	{
		m_IsShuttingDown = true;
		for (CaptureOutput& entry : m_Outputs)
		{
			if (entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled, "The runtime shut down before the capture was due.");
			}
		}
		Update();
	}

	void FrameCaptureCoordinator::FinalizeAfterRenderHost() noexcept
	{
		m_IsShuttingDown = true;
		// Drain certified readbacks before finalizing coverage that lacks evidence.
		Update();
		if (m_NonFiniteWatch)
		{
			m_NonFiniteWatch->FinalizeCoverage();
		}
		Update();
		FinishWriting();
		for (CaptureOutput& entry : m_Outputs)
		{
			if (entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled, "The runtime shut down before the capture was due.");
			}
			else if (entry.m_Phase == Phase::Issued || entry.m_Phase == Phase::AwaitingWatch)
			{
				FailPlan(*entry.m_Job, "The render host finalized without every required capture product.");
			}
		}
		m_Capture = nullptr;
	}

	bool FrameCaptureCoordinator::MatchesRequiredContent(
		const CaptureJob& job, const FrameCaptureFrameState& state) noexcept
	{
		const std::string& required = job.m_Plan.m_RequiredContentId;
		return required.empty() || required == state.m_DemoId || required == state.m_LabId;
	}

	bool FrameCaptureCoordinator::IsDue(
		const CaptureJob& job, const FrameCaptureFrameState& state) const noexcept
	{
		const CapturePlan& plan = job.m_Plan;
		if (!plan.m_ReferenceViewId.empty() && !job.m_ViewApplied)
		{
			return false;
		}
		return plan.m_Timing == FrameCaptureTiming::NextFrame ||
			(m_HistoryReady && MatchesRequiredContent(job, state) && m_SettledFrames >= plan.m_SettleFrames);
	}

	bool FrameCaptureCoordinator::HasFrameResourceExports() const noexcept
	{
		return std::ranges::any_of(m_Outputs, [](const CaptureOutput& entry)
			{
				return (entry.m_Phase == Phase::Waiting || entry.m_Phase == Phase::Issued) &&
					entry.GetSource() == FrameCaptureSource::ResourceExport;
			});
	}

	bool FrameCaptureCoordinator::PrepareResourceExports(uint64_t frameSerial, RenderGraph& graph,
		ResourceExportFrameContext context) noexcept
	{
		if (!m_Capture)
		{
			return true;
		}
		// Graph build may already have deferred an image belonging to a required
		// plan. Drain that outcome before deciding whether this frame may submit.
		Update();
		const bool strictWatch = m_NonFiniteWatch && m_NonFiniteWatch->GetStatus().m_Enabled &&
			m_NonFiniteWatch->GetStatus().m_Mode == NonFiniteWatchMode::Fail;
		const bool waitingExactExport = m_LastFrameState &&
			std::ranges::any_of(m_Outputs, [&](const CaptureOutput& entry)
			{
				const CaptureJob& job = *entry.m_Job;
				return entry.m_Phase == Phase::Waiting && (job.m_Plan.m_Sequence || strictWatch) &&
					entry.GetSource() == FrameCaptureSource::ResourceExport &&
					IsDue(job, *m_LastFrameState);
			});
		bool prepared = false;
		if (!waitingExactExport && m_AbortFrameSerial != frameSerial)
		{
			prepared = m_Capture->PrepareResourceExports(frameSerial, graph, context) !=
				FrameCaptureAvailability::Busy;
			// A permanent resource failure must fail its required image before any
			// sibling can submit. Runtime still retires provisional GPU owners.
			Update();
		}
		if (!prepared || m_AbortFrameSerial == frameSerial)
		{
			m_Capture->DeferFrameCaptures(frameSerial);
			m_AbortFrameSerial.reset();
			m_HasOpenFrame = false;
			Update();
			return false;
		}
		return true;
	}

	void FrameCaptureCoordinator::Issue(CaptureOutput& entry,
		const FrameCaptureFrameState& state, bool preAdmitted) noexcept
	{
		CaptureJob& job = *entry.m_Job;
		const CapturePlan& plan = job.m_Plan;
		if (!m_Capture)
		{
			FailPlan(job, "Frame capture is unavailable.");
			return;
		}
		if (state.m_FrameSerial == 0)
		{
			FailPlan(job, "Frame capture requires a live render frame serial.");
			return;
		}
		const auto tap = entry.GetDiagnosticTap();
		if (entry.GetSource() == FrameCaptureSource::Diagnostic &&
			(!tap || GetFrameCaptureDiagnosticTapName(*tap).empty()))
		{
			FailPlan(job, "A diagnostic capture requires a supported diagnostic tap.");
			return;
		}
		const auto availability = preAdmitted ?
			FrameCaptureAvailability::Available :
			GetCaptureAvailability(entry.GetSource(), state);
		if (availability != FrameCaptureAvailability::Available)
		{
			if (availability == FrameCaptureAvailability::Busy &&
				(entry.GetSource() == FrameCaptureSource::ResourceExport ||
					(plan.m_WaitForCapacity && !plan.m_Sequence)))
			{
				return;
			}
			const char* failure = "Capture capacity is unavailable before GPU submission.";
			if (availability == FrameCaptureAvailability::TooLarge)
			{
				failure = "The capture target exceeds the payload byte budget.";
			}
			else if (availability == FrameCaptureAvailability::Unsupported)
			{
				failure = "The display target has no capture footprint.";
			}
			FailPlan(job, failure);
			return;
		}
		GGLAB_ASSERT_MSG(!entry.m_Attempt, "A waiting capture output must not retain a product attempt.");
		ProductAttempt attempt;
		FrameCaptureMetadata& metadata = attempt.m_Metadata;
		metadata.m_RequestId = entry.m_Id;
		metadata.m_Label = plan.m_Label;
		metadata.m_Note = plan.m_Note;
		metadata.m_Source = entry.GetSource();
		metadata.m_Timing = plan.m_Timing;
		metadata.m_SettleFrames = plan.m_Timing == FrameCaptureTiming::AfterReady ? plan.m_SettleFrames : 0;
		metadata.m_SettledFrames = m_SettledFrames;
		metadata.m_Backend = state.m_Backend;
		metadata.m_DemoId = state.m_DemoId;
		metadata.m_LabId = state.m_LabId;
		metadata.m_AssetBundle = state.m_AssetBundle;
		metadata.m_FrameSerial = state.m_FrameSerial;
		metadata.m_FrameIndex = state.m_FrameIndex;
		metadata.m_Camera = state.m_Camera;
		metadata.m_Camera.m_ReferenceViewId = plan.m_ReferenceViewId;
		metadata.m_FixedDeltaTime = state.m_FixedDeltaTime;
		metadata.m_TotalTime = state.m_TotalTime;
		metadata.m_DevelopmentTools = state.m_DevelopmentTools;
		metadata.m_Readiness = state.m_Readiness;
		metadata.m_Temporal = state.m_Temporal;
		metadata.m_Sequence = plan.m_Sequence;
		metadata.m_DiagnosticTap = tap ? std::string(GetFrameCaptureDiagnosticTapName(*tap)) : std::string();
		metadata.m_CapturedAtUtc = std::format("{:%FT%TZ}",
			std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
		if (const auto* resources = std::get_if<ResourceCaptureProduct>(&entry.GetProduct()))
		{
			attempt.m_CaptureRequestId = m_Capture->RequestResourceExport(resources->m_Targets,
				state.m_FrameSerial, plan.m_Sequence.has_value() || job.m_OutputIds.size() > 1,
				entry.m_GraphIndex.has_value(), job.GetRuntimeGroupId());
		}
		else if (entry.GetSource() == FrameCaptureSource::Diagnostic)
		{
			attempt.m_CaptureRequestId = m_Capture->RequestDiagnosticCapture(
				*tap, state.m_FrameSerial, job.GetRuntimeGroupId());
		}
		else
		{
			attempt.m_CaptureRequestId = m_Capture->RequestCapture(
				entry.GetSource(), state.m_FrameSerial, job.GetRuntimeGroupId());
		}
		if (attempt.m_CaptureRequestId == 0)
		{
			FailPlan(job, "Frame capture request capacity is exhausted.");
			return;
		}
		if (!job.m_Attempt)
		{
			++job.m_AttemptCount;
			job.m_Attempt = FrameAttempt{
				state.m_FrameSerial, m_NonFiniteWatch ? m_NonFiniteWatch->OpenCoverage() : 0 };
		}
		GGLAB_ASSERT_MSG(job.m_Attempt->m_FrameSerial == state.m_FrameSerial,
			"Required products must bind to the same frame attempt.");
		entry.m_Attempt = std::move(attempt);
		entry.m_Phase = Phase::Issued;
	}

	void FrameCaptureCoordinator::FailPlan(CaptureJob& job, std::string failure) noexcept
	{
		if (job.m_Attempt && !job.m_Attempt->m_Submitted && job.m_OutputIds.size() > 1)
		{
			m_AbortFrameSerial = job.m_Attempt->m_FrameSerial;
		}
		for (CaptureOutput* member : GetOutputs(job))
		{
			GGLAB_ASSERT_MSG(member->m_Phase != Phase::Encoding,
				"Every required product and watch verdict must be ready before writing starts.");
			if (member->m_Phase == Phase::Issued && member->m_Attempt->m_CaptureRequestId != 0)
			{
				m_DiscardedCaptureResults.push_back(member->m_Attempt->m_CaptureRequestId);
			}
			Finish(*member, FrameCaptureRequestStatus::Failed, failure);
		}
	}

	void FrameCaptureCoordinator::RetryPlan(CaptureJob& job, uint64_t consumedRequestId) noexcept
	{
		GGLAB_ASSERT_MSG(job.m_Attempt && !job.m_Attempt->m_Submitted,
			"Busy may retry only an unsubmitted SameFrame attempt.");
		if (job.m_Plan.m_Sequence || job.m_OutputIds.size() > 1)
		{
			m_AbortFrameSerial = job.m_Attempt->m_FrameSerial;
		}
		for (CaptureOutput* member : GetOutputs(job))
		{
			GGLAB_ASSERT_MSG(member->m_Phase == Phase::Issued || member->m_Phase == Phase::AwaitingWatch,
				"A deferred required-product plan must not have started writing.");
			if (member->m_Phase == Phase::Issued && member->m_Attempt->m_CaptureRequestId != consumedRequestId &&
				member->m_Attempt->m_CaptureRequestId != 0)
			{
				m_DiscardedCaptureResults.push_back(member->m_Attempt->m_CaptureRequestId);
			}
			member->m_Attempt.reset();
			member->m_ResourceFailure = RGObservationFailure::None;
			member->m_Phase = Phase::Waiting;
		}
		if (m_NonFiniteWatch)
		{
			m_NonFiniteWatch->ReleaseCoverage(job.m_Attempt->m_WatchCoverageId);
		}
		job.m_Attempt.reset();
	}

	void FrameCaptureCoordinator::HandleCaptureResult(FrameCaptureResult result) noexcept
	{
		const auto entry = std::ranges::find_if(m_Outputs, [&](const CaptureOutput& candidate)
			{
				return candidate.m_Phase == Phase::Issued &&
					candidate.m_Attempt->m_CaptureRequestId == result.m_RequestId;
			});
		if (entry == m_Outputs.end())
		{
			if (std::erase(m_DrainingCaptureRequests, result.m_RequestId) != 0)
			{
				return;
			}
			if (std::erase(m_DiscardedCaptureResults, result.m_RequestId) != 0)
			{
				return;
			}
			GGLAB_LOG_WARN("Frame capture result {} has no coordinator request.", result.m_RequestId);
			return;
		}
		CaptureJob& job = *entry->m_Job;
		ProductAttempt& attempt = *entry->m_Attempt;
		const FrameCaptureSource source = entry->GetSource();
		attempt.m_CaptureRequestId = 0;
		if (result.m_Status == FrameCaptureStatus::Busy)
		{
			if (result.m_FrameSerial != attempt.m_Metadata.m_FrameSerial ||
				result.m_Source != source || job.m_Attempt->m_Submitted)
			{
				FailPlan(job,
					"The deferred capture result does not match an unsubmitted requested frame and source.");
				return;
			}
			// End every product association together; late sibling results belong
			// to this discarded attempt, never to the next frame's metadata.
			RetryPlan(job, result.m_RequestId);
			return;
		}
		const bool resources = source == FrameCaptureSource::ResourceExport;
		const bool graph = source == FrameCaptureSource::RenderGraph;
		entry->m_ResourceFailure = result.m_ResourceFailure;
		if (result.m_Status != FrameCaptureStatus::Completed ||
			(resources ?
				(!result.m_Resources || result.m_Resources->m_Arrays.empty() ||
					(entry->m_GraphIndex && !result.m_Graph)) :
				graph ? !result.m_Graph : !result.m_Image))
		{
			FailPlan(job, result.m_Failure.empty() ?
				std::string("The frame capture failed.") :
				std::move(result.m_Failure));
			return;
		}
		const auto expectedTap = source == FrameCaptureSource::Diagnostic ?
			entry->GetDiagnosticTap() :
			std::nullopt;
		if (result.m_FrameSerial != attempt.m_Metadata.m_FrameSerial ||
			result.m_Source != source || result.m_DiagnosticTap != expectedTap)
		{
			FailPlan(job,
				"The capture result does not match its requested render frame, source or diagnostic tap.");
			return;
		}
		attempt.m_PendingResult = std::move(result);
		entry->m_Phase = Phase::AwaitingWatch;
	}

	void FrameCaptureCoordinator::PollWatchCaptures() noexcept
	{
		const std::string failure =
			m_NonFiniteWatch ? m_NonFiniteWatch->GetFailModeFailure() : std::string{};
		std::vector<CaptureJob*> visited;
		for (CaptureOutput& entry : m_Outputs)
		{
			CaptureJob& job = *entry.m_Job;
			if (entry.m_Phase == Phase::Finished || entry.m_Phase == Phase::Encoding ||
				std::ranges::find(visited, &job) != visited.end())
			{
				continue;
			}
			visited.push_back(&job);
			if (entry.m_Phase == Phase::Waiting)
			{
				if (job.m_Plan.m_Timing == FrameCaptureTiming::AfterReady && !failure.empty())
				{
					FailPlan(job, failure);
				}
				continue;
			}
			const auto outputs = GetOutputs(job);
			const auto coverage = m_NonFiniteWatch ?
				m_NonFiniteWatch->GetCoverage(job.m_Attempt->m_WatchCoverageId) :
				nullptr;
			for (CaptureOutput* member : outputs)
			{
				member->m_Attempt->m_Metadata.m_WatchCoverage = coverage;
			}
			if (coverage && coverage->Fails())
			{
				FailPlan(job, coverage->m_Failure);
				continue;
			}
			if ((coverage && !coverage->m_Complete) ||
				!std::ranges::all_of(outputs, [](const auto* member) { return member->m_Phase == Phase::AwaitingWatch; }))
			{
				continue;
			}
			if (job.m_ReceiptSlot)
			{
				StartBundleEncoding(job);
				continue;
			}
			for (CaptureOutput* member : outputs)
			{
				// A synchronous encode failure can destroy this product attempt.
				FrameCaptureResult result = std::move(*member->m_Attempt->m_PendingResult);
				member->m_Attempt->m_PendingResult.reset();
				StartEncoding(*member, result);
			}
		}
	}

	CaptureProductEncodeInput FrameCaptureCoordinator::MakeEncodeInput(
		CaptureOutput& entry, const FrameCaptureResult& result) noexcept
	{
		FrameCaptureMetadata& metadata = entry.m_Attempt->m_Metadata;
		metadata.m_GraphExportActive = result.m_GraphExportActive || bool(result.m_Graph);
		metadata.m_ResourceExportActive = result.m_ResourceExportActive;
		metadata.m_NonFiniteScanActive = result.m_NonFiniteScanActive;
		if (result.m_Image)
		{
			const FrameCaptureImage& image = *result.m_Image;
			metadata.m_Width = image.m_Width;
			metadata.m_Height = image.m_Height;
			metadata.m_DisplayFormat = GetRHIFormatInfo(image.m_Format).m_Name;
		}
		return {
			.m_Image = result.m_Image,
			.m_Graph = result.m_Graph,
			.m_Resources = result.m_Resources,
			.m_ResourceExportActive = result.m_ResourceExportActive,
			.m_GraphSummary = entry.IsGraphSummary(),
			.m_Submission = { result.m_SubmissionFenceIndex, result.m_SubmissionFenceGeneration,
				result.m_SubmissionFenceValue, result.m_AdapterCompatibilityIdentity },
			.m_Metadata = metadata,
		};
	}

	void FrameCaptureCoordinator::StartBundleEncoding(CaptureJob& job) noexcept
	{
		const auto directory = job.m_Plan.m_OutputDirectory.empty() ?
			m_DefaultOutputDirectory :
			job.m_Plan.m_OutputDirectory;
		if (directory.empty())
		{
			FailPlan(job, "No capture output directory is configured.");
			return;
		}
		const auto outputs = GetOutputs(job);
		auto write = std::make_shared<CaptureWriteJob>();
		write->m_Bundle.emplace(CaptureBundleInput{ .m_RunIdentity = m_RunIdentity,
			.m_JobId = job.m_Result.m_JobId, .m_Attempt = job.m_AttemptCount,
			.m_ProductCount = static_cast<uint32_t>(job.m_Plan.m_RequiredProducts.size()) });
		for (CaptureOutput* member : outputs)
		{
			const FrameCaptureResult result = std::move(*member->m_Attempt->m_PendingResult);
			member->m_Attempt->m_PendingResult.reset();
			write->m_Bundle->m_Deliveries.push_back({
				.m_ProductIndex = static_cast<uint32_t>(member->m_ProductIndex),
				.m_GraphProductIndex = member->m_GraphIndex ?
					std::optional(static_cast<uint32_t>(*member->m_GraphIndex)) : std::nullopt,
				.m_Input = MakeEncodeInput(*member, result),
			});
		}
		job.m_Result.m_BundleId = MakeCaptureBundleId(m_RunIdentity, job.m_Result.m_JobId, job.m_AttemptCount);
		std::string subject = job.m_Plan.m_Label.empty() ? "capture" : SanitizeFileStemComponent(job.m_Plan.m_Label);
		subject.resize(std::min(subject.size(), size_t{ 32 }));
		write->m_BaseStem = std::format("{}.{}", subject, job.m_Result.m_BundleId);
		write->m_Directory = directory;
		write->m_RequestId = job.m_OutputIds.front();
		for (CaptureOutput* member : outputs)
		{
			member->m_Phase = Phase::Encoding;
			member->m_WriteJob = write;
		}
		if (!m_Writer->Submit(std::move(write)))
		{
			FinishBundleWriting(job, { .m_Failure = "Capture writer queue is full." });
		}
	}

	void FrameCaptureCoordinator::FinishBundleWriting(
		CaptureJob& job, const CaptureWriteReceipt& receipt) noexcept
	{
		job.m_Result.m_ManifestPath = receipt.m_ManifestPath;
		for (CaptureOutput* member : GetOutputs(job))
		{
			member->m_WriteReceipt = receipt;
			if (receipt.m_Publication == CapturePublicationOutcome::Published)
			{
				const auto product = std::ranges::find(receipt.m_BundleProducts,
					member->m_ProductIndex, &CaptureBundleProductReceipt::m_ProductIndex);
				GGLAB_ASSERT_MSG(product != receipt.m_BundleProducts.end(), "A published bundle retains every required product.");
				for (const auto& path : product->m_Files)
				{
					if (path.extension() == ".png")
					{
						member->m_Attempt->m_Metadata.m_ImageFile = GetFrameCapturePathText(path.filename());
					}
				}
			}
			Finish(*member, receipt.m_Publication == CapturePublicationOutcome::Published ?
				FrameCaptureRequestStatus::Completed : FrameCaptureRequestStatus::Failed, receipt.m_Failure);
		}
	}

	void FrameCaptureCoordinator::StartEncoding(
		CaptureOutput& entry, const FrameCaptureResult& result) noexcept
	{
		const CapturePlan& plan = entry.m_Job->m_Plan;
		const std::filesystem::path directory = plan.m_OutputDirectory.empty() ?
			m_DefaultOutputDirectory :
			plan.m_OutputDirectory;
		if (directory.empty())
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "No capture output directory is configured.");
			return;
		}
		auto products = MakeEncodeInput(entry, result);
		const FrameCaptureMetadata& metadata = entry.m_Attempt->m_Metadata;
		const std::string_view subject = !plan.m_Label.empty() ?
			std::string_view(plan.m_Label) :
			!metadata.m_LabId.empty() ?
				std::string_view(metadata.m_LabId) :
				std::string_view(metadata.m_DemoId);
		const std::string timestamp = metadata.m_CapturedAtUtc.size() >= 19 ?
			std::format("{}{}{}-{}{}{}", metadata.m_CapturedAtUtc.substr(0, 4),
				metadata.m_CapturedAtUtc.substr(5, 2), metadata.m_CapturedAtUtc.substr(8, 2),
				metadata.m_CapturedAtUtc.substr(11, 2), metadata.m_CapturedAtUtc.substr(14, 2),
				metadata.m_CapturedAtUtc.substr(17, 2)) :
			std::string("unknown-time");
		const std::string view = metadata.m_Camera.m_ReferenceViewId.empty() ?
			std::string{} :
			"-" + SanitizeFileStemComponent(metadata.m_Camera.m_ReferenceViewId);
		const std::string baseStem = std::format("{}{}-{}-{}-{}-r{}",
			SanitizeFileStemComponent(subject), view, GetFrameCaptureSourceName(metadata.m_Source),
			SanitizeFileStemComponent(metadata.m_Backend), timestamp, entry.m_Id);
		auto job = std::make_shared<CaptureWriteJob>();
		job->m_Products = std::move(products);
		job->m_Directory = directory;
		job->m_BaseStem = result.m_Resources ?
			std::format("{}.{}.resource-export", SanitizeFileStemComponent(subject), metadata.m_FrameSerial) :
			result.m_Graph ?
				std::format("{}.{}.graph{}", SanitizeFileStemComponent(subject), metadata.m_FrameSerial,
					entry.IsGraphSummary() ? "-summary" : "") :
				baseStem;
		job->m_RequestId = entry.m_Id;
		entry.m_WriteJob = job;
		entry.m_Phase = Phase::Encoding;
		if (!m_Writer->Submit(std::move(job)))
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Capture writer queue is full.");
		}
	}

	bool FrameCaptureCoordinator::TryFinishWritten(CaptureOutput& entry) noexcept
	{
		auto receipt = entry.m_WriteJob->GetReceipt();
		if (!receipt)
		{
			return false;
		}
		if (entry.m_Job->m_ReceiptSlot)
		{
			FinishBundleWriting(*entry.m_Job, *receipt);
			return true;
		}
		entry.m_WriteReceipt = std::move(receipt);
		const FrameCaptureRequestStatus status =
			entry.m_WriteReceipt->m_Publication == CapturePublicationOutcome::Published ?
				FrameCaptureRequestStatus::Completed :
				FrameCaptureRequestStatus::Failed;
		if (status == FrameCaptureRequestStatus::Completed && !entry.m_WriteReceipt->m_ImagePath.empty())
		{
			entry.m_Attempt->m_Metadata.m_ImageFile =
				GetFrameCapturePathText(entry.m_WriteReceipt->m_ImagePath.filename());
		}
		Finish(entry, status, entry.m_WriteReceipt->m_Failure);
		return true;
	}

	void FrameCaptureCoordinator::CollectWrittenJobs() noexcept
	{
		CollectDrainingWrites();
		for (CaptureOutput& entry : m_Outputs)
		{
			if (entry.m_Phase != Phase::Encoding || TryFinishWritten(entry))
			{
				continue;
			}
			CaptureWriteJob& job = *entry.m_WriteJob;
			if (!job.m_StallReported &&
				std::chrono::steady_clock::now() - job.m_SubmittedAt >= EncodeStallReportTime)
			{
				job.m_StallReported = true;
				GGLAB_LOG_WARN_ALWAYS(
					"Frame capture {} has not finished encoding after {} seconds (stage: {}).",
					entry.m_Id, EncodeStallReportTime.count(),
					GetCaptureWriteStageName(job.m_Publication.m_Stage));
			}
		}
	}

	void FrameCaptureCoordinator::CollectDrainingWrites() noexcept
	{
		std::erase_if(m_DrainingWrites, [](const DrainingWrite& write)
			{
				return write.m_Job->GetReceipt().has_value();
			});
	}

	void FrameCaptureCoordinator::FinishWriting() noexcept
	{
		// One deadline bounds all unfinished outputs, not one timeout per writer job.
		const auto deadline = std::chrono::steady_clock::now() + m_ShutdownWriteTimeout;
		for (CaptureOutput& entry : m_Outputs)
		{
			if (entry.m_Phase != Phase::Encoding)
			{
				continue;
			}
			entry.m_WriteJob->WaitUntil(deadline);
			if (TryFinishWritten(entry))
			{
				continue;
			}
			auto receipt = m_Writer->Abandon(*entry.m_WriteJob, m_ShutdownWriteTimeout);
			if (entry.m_Job->m_ReceiptSlot)
			{
				FinishBundleWriting(*entry.m_Job, receipt);
				continue;
			}
			entry.m_WriteReceipt = std::move(receipt);
			if (entry.m_WriteReceipt->m_Publication == CapturePublicationOutcome::Published)
			{
				// Completion may race the timeout snapshot; consume that receipt normally.
				GGLAB_UNUSED(TryFinishWritten(entry));
			}
			else
			{
				Finish(entry, FrameCaptureRequestStatus::Failed, entry.m_WriteReceipt->m_Failure);
			}
		}
		for (DrainingWrite& write : m_DrainingWrites)
		{
			if (write.m_AbandonedAtShutdown)
			{
				continue;
			}
			write.m_Job->WaitUntil(deadline);
			if (!write.m_Job->GetReceipt())
			{
				GGLAB_UNUSED(m_Writer->Abandon(*write.m_Job, m_ShutdownWriteTimeout));
				write.m_AbandonedAtShutdown = true;
			}
		}
		CollectDrainingWrites();
	}

	void FrameCaptureCoordinator::Finish(
		CaptureOutput& entry, FrameCaptureRequestStatus status, std::string failure) noexcept
	{
		GGLAB_ASSERT_MSG(entry.m_Phase != Phase::Finished, "A capture output must finish exactly once.");
		CaptureJob& job = *entry.m_Job;
		FrameCaptureRequestResult result{
			.m_RequestId = entry.m_Id,
			.m_Status = status,
			.m_Failure = std::move(failure),
		};
		if (entry.m_WriteReceipt && status == FrameCaptureRequestStatus::Completed)
		{
			result.m_ImagePath = entry.m_WriteReceipt->m_ImagePath;
			result.m_MetadataPath = entry.m_WriteReceipt->m_MetadataPath;
			result.m_ResourcePaths = entry.m_WriteReceipt->m_ResourcePaths;
			result.m_GraphPath = entry.m_WriteReceipt->m_GraphPath;
		}
		if (entry.m_Attempt)
		{
			FrameCaptureMetadata& metadata = entry.m_Attempt->m_Metadata;
			if (m_NonFiniteWatch && job.m_Attempt && !metadata.m_WatchCoverage)
			{
				metadata.m_WatchCoverage = m_NonFiniteWatch->GetCoverage(job.m_Attempt->m_WatchCoverageId);
			}
			result.m_Metadata = metadata;
		}
		result.m_ResourceFailure = entry.m_ResourceFailure;
		if (job.m_ReceiptSlot)
		{
			RecordJobOutput(entry, result);
		}
		else
		{
			m_Results.push_back(std::move(result));
		}
		entry.m_Attempt.reset();
		entry.m_WriteJob.reset();
		entry.m_Phase = Phase::Finished;
		GGLAB_ASSERT_MSG(job.m_RemainingOutputs != 0, "A plan must retain every unfinished output.");
		if (--job.m_RemainingOutputs == 0)
		{
			if (job.m_ReceiptSlot)
			{
				CompleteJob(job);
			}
			if (m_NonFiniteWatch && job.m_Attempt)
			{
				m_NonFiniteWatch->ReleaseCoverage(job.m_Attempt->m_WatchCoverageId);
			}
			job.m_Attempt.reset();
		}
	}

	void FrameCaptureCoordinator::RecordJobOutput(
		CaptureOutput& entry, const FrameCaptureRequestResult& result) noexcept
	{
		CaptureJob& job = *entry.m_Job;
		const auto record = [&](size_t index, bool graph) noexcept
			{
				CaptureProductResult& product = job.m_Result.m_Products[index];
				product.m_ProductIndex = static_cast<uint32_t>(index);
				switch (result.m_Status)
				{
				case FrameCaptureRequestStatus::Completed:
					product.m_Status = CaptureJobStatus::Completed;
					break;
				case FrameCaptureRequestStatus::Failed:
					product.m_Status = CaptureJobStatus::Failed;
					job.m_Result.m_Status = CaptureJobStatus::Failed;
					break;
				case FrameCaptureRequestStatus::Cancelled:
					product.m_Status = CaptureJobStatus::Cancelled;
					if (job.m_Result.m_Status != CaptureJobStatus::Failed)
					{
						job.m_Result.m_Status = CaptureJobStatus::Cancelled;
					}
					break;
				}
				if (entry.m_WriteReceipt)
				{
					switch (entry.m_WriteReceipt->m_Publication)
					{
					case CapturePublicationOutcome::Published:
						product.m_Publication = CaptureJobPublication::Published;
						break;
					case CapturePublicationOutcome::Indeterminate:
						product.m_Publication = CaptureJobPublication::Indeterminate;
						break;
					case CapturePublicationOutcome::NotPublished:
						break;
					}
				}
				if (entry.m_WriteReceipt && !entry.m_WriteReceipt->m_BundleProducts.empty())
				{
					const auto files = std::ranges::find(entry.m_WriteReceipt->m_BundleProducts,
						index, &CaptureBundleProductReceipt::m_ProductIndex);
					GGLAB_ASSERT_MSG(files != entry.m_WriteReceipt->m_BundleProducts.end(),
						"A bundle receipt retains every product's files.");
					product.m_Files = files->m_Files;
				}
				else if (graph)
				{
					if (!result.m_GraphPath.empty())
					{
						product.m_Files.push_back(result.m_GraphPath);
					}
				}
				else
				{
					for (const auto& path : { result.m_ImagePath, result.m_MetadataPath })
					{
						if (!path.empty())
						{
							product.m_Files.push_back(path);
						}
					}
					product.m_Files.insert(product.m_Files.end(),
						result.m_ResourcePaths.begin(), result.m_ResourcePaths.end());
				}
				product.m_ResourceFailure = result.m_ResourceFailure;
				product.m_Failure = result.m_Failure;
				product.m_Metadata = result.m_Metadata;
				++job.m_FinishedProducts;
			};
		record(entry.m_ProductIndex, entry.GetSource() == FrameCaptureSource::RenderGraph);
		if (entry.m_GraphIndex)
		{
			record(*entry.m_GraphIndex, true);
		}
	}

	void FrameCaptureCoordinator::CompleteJob(CaptureJob& job) noexcept
	{
		JobReceipt& slot = m_JobReceipts[*job.m_ReceiptSlot];
		GGLAB_ASSERT_MSG(!slot.m_Result && slot.m_JobId == job.m_Result.m_JobId,
			"Every accepted job owns exactly one reserved terminal receipt.");
		uint32_t published = 0;
		bool indeterminate = false;
		for (const CaptureProductResult& product : job.m_Result.m_Products)
		{
			published += product.m_Publication == CaptureJobPublication::Published;
			indeterminate |= product.m_Publication == CaptureJobPublication::Indeterminate;
		}
		job.m_Result.m_Publication = indeterminate ?
			CaptureJobPublication::Indeterminate :
			published == job.m_Result.m_Products.size() ?
				CaptureJobPublication::Published :
				published != 0 ?
					CaptureJobPublication::PartiallyPublished :
					CaptureJobPublication::NotPublished;
		slot.m_FinalSnapshot = { .m_JobId = slot.m_JobId, .m_Stage = CaptureJobStage::Finished,
			.m_AttemptCount = job.m_AttemptCount,
			.m_FrameSerial = job.m_Attempt ? std::optional(job.m_Attempt->m_FrameSerial) : std::nullopt,
			.m_RequiredProducts = static_cast<uint32_t>(job.m_Plan.m_RequiredProducts.size()),
			.m_FinishedProducts = job.m_FinishedProducts };
		slot.m_Result = std::move(job.m_Result);
		slot.m_Job.reset();
		GGLAB_ASSERT_MSG(m_ActiveJobs != 0, "A terminal job returns one active-job credit.");
		--m_ActiveJobs;
	}

	void FrameCaptureCoordinator::RemoveFinishedJobs() noexcept
	{
		std::erase_if(m_Outputs, [](const CaptureOutput& entry) { return entry.m_Phase == Phase::Finished; });
	}
}
