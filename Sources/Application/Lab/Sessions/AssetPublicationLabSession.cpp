#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/AssetPublicationLabSession.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	namespace
	{
		const LabParameterId ScenarioId("asset_publication.scenario");
		const LabParameterId FaultOccurrenceId("asset_publication.fault_occurrence");

		[[nodiscard]] const char* ScenarioText(
			AssetPublicationLabSession::Scenario scenario) noexcept
		{
			switch (scenario)
			{
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return "Incremental Success";
			case AssetPublicationLabSession::Scenario::CancelTextures:
				return "Cancel During Textures";
			case AssetPublicationLabSession::Scenario::FailMaterials:
				return "Fail During Materials";
			case AssetPublicationLabSession::Scenario::CancelMeshes:
				return "Cancel During Meshes";
			case AssetPublicationLabSession::Scenario::FailMeshInstances:
				return "Fail During Mesh Instances";
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return "Cancel During Dependencies";
			}
			return "Unknown";
		}

		[[nodiscard]] std::filesystem::path ScenarioModelPath(
			AssetPublicationLabSession::Scenario scenario)
		{
			switch (scenario)
			{
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return "Assets/Models/FlightHelmet/FlightHelmet.gltf";
			case AssetPublicationLabSession::Scenario::CancelTextures:
				return "Assets/Models/Sponza/Sponza.gltf";
			case AssetPublicationLabSession::Scenario::FailMaterials:
				return "Assets/Models/MetalRoughSpheres/MetalRoughSpheres.gltf";
			case AssetPublicationLabSession::Scenario::CancelMeshes:
				return "Assets/Models/NormalTangentMirrorTest/NormalTangentMirrorTest.gltf";
			case AssetPublicationLabSession::Scenario::FailMeshInstances:
				return "Assets/Models/NormalTangentTest/NormalTangentTest.gltf";
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return "Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf";
			}
			return {};
		}

		[[nodiscard]] AssetResourcePublicationStage FaultStage(
			AssetPublicationLabSession::Scenario scenario) noexcept
		{
			switch (scenario)
			{
			case AssetPublicationLabSession::Scenario::CancelTextures:
				return AssetResourcePublicationStage::Textures;
			case AssetPublicationLabSession::Scenario::FailMaterials:
				return AssetResourcePublicationStage::Materials;
			case AssetPublicationLabSession::Scenario::CancelMeshes:
				return AssetResourcePublicationStage::Meshes;
			case AssetPublicationLabSession::Scenario::FailMeshInstances:
				return AssetResourcePublicationStage::MeshInstances;
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return AssetResourcePublicationStage::Dependencies;
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return AssetResourcePublicationStage::Unknown;
			}
			return AssetResourcePublicationStage::Unknown;
		}

		[[nodiscard]] AssetResourcePublicationFaultAction FaultAction(
			AssetPublicationLabSession::Scenario scenario) noexcept
		{
			switch (scenario)
			{
			case AssetPublicationLabSession::Scenario::FailMaterials:
			case AssetPublicationLabSession::Scenario::FailMeshInstances:
				return AssetResourcePublicationFaultAction::Fail;
			case AssetPublicationLabSession::Scenario::CancelTextures:
			case AssetPublicationLabSession::Scenario::CancelMeshes:
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return AssetResourcePublicationFaultAction::Cancel;
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return AssetResourcePublicationFaultAction::None;
			}
			return AssetResourcePublicationFaultAction::None;
		}

		[[nodiscard]] bool HasPendingIdentity(
			const AssetStreamingQueueStatistics& queue,
			const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(queue.m_PendingWork,
				[identity](const AssetStreamingWorkActivity& work) noexcept
				{
					return work.m_Identity == identity;
				});
		}
	}

	struct AssetPublicationLabSession::ScenarioState
	{
		Scenario m_Scenario = Scenario::IncrementalSuccess;
		AssetManager::ModelLoadRequest m_Request{};
		AssetStreamingIdentity m_Identity{};
		std::filesystem::path m_ModelPath;
		AssetOwnershipStatistics m_BaselineOwnership{};
		uint64_t m_StartEnqueued = 0;
		uint64_t m_StartProcessed = 0;
		uint64_t m_StartContinue = 0;
		uint64_t m_StartCompleted = 0;
		uint64_t m_StartFailed = 0;
		uint64_t m_StartCancelled = 0;
		uint64_t m_StartFaultInjections = 0;
		uint64_t m_StartNoProgressContinues = 0;
		uint64_t m_LastProcessed = 0;
		uint32_t m_FramesWithPublicationSteps = 0;
		uint32_t m_SettleFrames = 0;
		float m_ElapsedSeconds = 0.0f;
		AssetState m_FinalModelState = AssetState::Unloaded;
		bool m_SawPublicationQueue = false;
		bool m_Finished = false;
		bool m_Passed = false;
		bool m_TimedOut = false;
		std::vector<std::string> m_Errors;
	};

	AssetPublicationLabSession::AssetPublicationLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ScenarioId,
			.m_Name = "Scenario",
			.m_Group = "Verification",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(0),
			.m_EnumItems = {
				{ .m_Value = 0, .m_Name = "Incremental Success" },
				{ .m_Value = 1, .m_Name = "Cancel During Textures" },
				{ .m_Value = 2, .m_Name = "Fail During Materials" },
				{ .m_Value = 3, .m_Name = "Cancel During Meshes" },
				{ .m_Value = 4, .m_Name = "Fail During Mesh Instances" },
				{ .m_Value = 5, .m_Name = "Cancel During Dependencies" },
			},
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FaultOccurrenceId,
			.m_Name = "Fault Step Occurrence",
			.m_Group = "Verification",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(1),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(uint32_t(64)),
		}));
		ApplyImmediateParameters();
	}

	void AssetPublicationLabSession::OnEnter() noexcept
	{
		m_Entered = true;
		StartScenario();
	}

	void AssetPublicationLabSession::OnExit() noexcept
	{
		StopScenario();
		m_Entered = false;
	}

	void AssetPublicationLabSession::Update(float deltaTime) noexcept
	{
		GetCamera().Update();
		if (!m_State || m_State->m_Finished)
		{
			return;
		}

		m_State->m_ElapsedSeconds += deltaTime;
		AssetUploadScheduler* scheduler =
			m_Services.m_Renderer->GetAssetUploadScheduler();
		const AssetUploadStatistics statistics = scheduler->GetStatistics();
		const uint64_t processed =
			statistics.m_ResourcePublicationQueue.m_ProcessedCount;
		if (processed > m_State->m_LastProcessed)
		{
			++m_State->m_FramesWithPublicationSteps;
			m_State->m_LastProcessed = processed;
		}
		m_State->m_SawPublicationQueue |= HasPendingIdentity(
			statistics.m_ResourcePublicationQueue,
			m_State->m_Identity);

		const Model* model = m_Services.m_AssetManager->GetModel(
			m_State->m_Request.m_ModelId);
		if (model && model->m_Generation == m_State->m_Request.m_Generation)
		{
			m_State->m_FinalModelState = model->m_State;
			const bool terminal = model->m_State == AssetState::Ready ||
				model->m_State == AssetState::Failed ||
				model->m_State == AssetState::Cancelled;
			if (terminal && !HasPendingIdentity(
				statistics.m_ResourcePublicationQueue,
				m_State->m_Identity))
			{
				++m_State->m_SettleFrames;
			}
		}

		if (m_State->m_SettleFrames >= 8)
		{
			EvaluateScenario(statistics);
		}
		else if (m_State->m_ElapsedSeconds > 90.0f)
		{
			m_State->m_TimedOut = true;
			m_State->m_Finished = true;
			m_State->m_Passed = false;
			m_State->m_Errors.push_back(
				"Scenario exceeded the 90 second timeout before reaching a stable terminal state.");
		}
	}

	void AssetPublicationLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Asset Publication Verification";
		if (!m_State)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Scenario state",
				.m_Status = LabDiagnosticCheckStatus::Pending,
				.m_Detail = "No active scenario.",
			});
			return;
		}

		const AssetOwnershipStatistics ownership =
			m_Services.m_AssetManager->GetOwnershipStatistics();
		diagnostics.m_Metrics = {
			{ .m_Name = "Scenario", .m_Value = ScenarioText(m_State->m_Scenario) },
			{ .m_Name = "Model", .m_Value = m_State->m_ModelPath.generic_string() },
			{ .m_Name = "Model ID / generation",
				.m_Value = std::format("{} / {}",
					m_State->m_Request.m_ModelId.Value(),
					m_State->m_Request.m_Generation) },
			{ .m_Name = "Frames with publication steps",
				.m_Value = std::to_string(m_State->m_FramesWithPublicationSteps) },
			{ .m_Name = "Active publication retains",
				.m_Value = std::to_string(ownership.m_PublicationRetainCount) },
			{ .m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_ElapsedSeconds) },
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "Scenario result",
			.m_Status = !m_State->m_Finished ? LabDiagnosticCheckStatus::Pending :
				m_State->m_Passed ? LabDiagnosticCheckStatus::Passed :
				LabDiagnosticCheckStatus::Failed,
			.m_Detail = !m_State->m_Finished ? "Scenario is running." :
				m_State->m_Passed ? "All publication invariants passed." :
				"One or more publication invariants failed.",
		});
		for (const std::string& error : m_State->m_Errors)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Invariant",
				.m_Status = LabDiagnosticCheckStatus::Failed,
				.m_Detail = error,
			});
		}
	}

	void AssetPublicationLabSession::ApplyImmediateParameters() noexcept
	{
		m_Scenario = static_cast<Scenario>(
			GetParameters().Get(ScenarioId, int32_t(0)));
		m_FaultOccurrence = GetParameters().Get(
			FaultOccurrenceId,
			uint32_t(1));
		if (m_Entered)
		{
			StartScenario();
		}
	}

	void AssetPublicationLabSession::StartScenario() noexcept
	{
		StopScenario();
		AssetUploadScheduler* scheduler =
			m_Services.m_Renderer->GetAssetUploadScheduler();
		if (!m_HasOriginalBudget)
		{
			m_OriginalBudget = scheduler->GetFrameBudget();
			m_HasOriginalBudget = true;
		}
		AssetStreamingFrameBudget stressBudget = m_OriginalBudget;
		stressBudget.m_MaxResourcePublicationSteps = 1;
		stressBudget.m_MaxResourcePublicationCreations = 1;
		stressBudget.m_MaxResourcePublicationMilliseconds = 0.05;
		scheduler->SetFrameBudget(stressBudget);

		m_State = std::make_unique<ScenarioState>();
		m_State->m_Scenario = m_Scenario;
		m_State->m_ModelPath = ScenarioModelPath(m_Scenario);
		m_State->m_BaselineOwnership =
			m_Services.m_AssetManager->GetOwnershipStatistics();
		const AssetUploadStatistics before = scheduler->GetStatistics();
		const auto& publicationBefore = before.m_ResourcePublicationQueue;
		m_State->m_StartEnqueued = publicationBefore.m_EnqueuedCount;
		m_State->m_StartProcessed = publicationBefore.m_ProcessedCount;
		m_State->m_StartContinue = publicationBefore.m_ContinueCount;
		m_State->m_StartCompleted = publicationBefore.m_CompletedCount;
		m_State->m_StartFailed = publicationBefore.m_FailedCount;
		m_State->m_StartCancelled = publicationBefore.m_CancelledCount;
		m_State->m_StartFaultInjections = publicationBefore.m_FaultInjectionCount;
		m_State->m_StartNoProgressContinues =
			publicationBefore.m_NoProgressContinueCount;
		m_State->m_LastProcessed = publicationBefore.m_ProcessedCount;

		m_State->m_Request = GetAssetOwnerScope().LoadModelAsync(
			m_State->m_ModelPath,
			TaskPriority::Normal);
		if (!m_State->m_Request.IsValid())
		{
			m_State->m_Finished = true;
			m_State->m_Errors.push_back("AssetManager rejected the model load request.");
			return;
		}
		m_State->m_Identity = {
			.m_Kind = AssetStreamingWorkKind::Model,
			.m_StableId = m_State->m_Request.m_ModelId.Value(),
			.m_Generation = m_State->m_Request.m_Generation,
		};

		const AssetResourcePublicationFaultAction action = FaultAction(m_Scenario);
		if (action != AssetResourcePublicationFaultAction::None)
		{
			scheduler->ArmResourcePublicationFault({
				.m_Identity = m_State->m_Identity,
				.m_Stage = FaultStage(m_Scenario),
				.m_Action = action,
				.m_TriggerOccurrence = m_FaultOccurrence,
			});
		}
	}

	void AssetPublicationLabSession::StopScenario() noexcept
	{
		if (m_Services.m_Renderer)
		{
			AssetUploadScheduler* scheduler =
				m_Services.m_Renderer->GetAssetUploadScheduler();
			scheduler->ClearResourcePublicationFault();
			if (m_HasOriginalBudget)
			{
				scheduler->SetFrameBudget(m_OriginalBudget);
			}
		}
		ResetAssetInterests();
		m_State.reset();
	}

	void AssetPublicationLabSession::EvaluateScenario(
		const AssetUploadStatistics& statistics) noexcept
	{
		GGLAB_ASSERT(m_State);
		const auto& publication = statistics.m_ResourcePublicationQueue;
		const AssetOwnershipStatistics ownership =
			m_Services.m_AssetManager->GetOwnershipStatistics();
		const bool successScenario =
			m_State->m_Scenario == Scenario::IncrementalSuccess;
		const AssetResourcePublicationFaultAction action =
			FaultAction(m_State->m_Scenario);
		const AssetState expectedState = successScenario ? AssetState::Ready :
			action == AssetResourcePublicationFaultAction::Fail ?
				AssetState::Failed : AssetState::Cancelled;

		const auto require = [this](bool condition, std::string error)
		{
			if (!condition)
			{
				m_State->m_Errors.push_back(std::move(error));
			}
		};
		require(m_State->m_FinalModelState == expectedState,
			std::format("Expected terminal model state {}, observed {}.",
				static_cast<uint32_t>(expectedState),
				static_cast<uint32_t>(m_State->m_FinalModelState)));
		require(publication.m_EnqueuedCount == m_State->m_StartEnqueued + 1,
			"The scenario did not enqueue exactly one resource publication job.");
		require(publication.m_ProcessedCount > m_State->m_StartProcessed,
			"The publication job executed no steps.");
		require(publication.m_NoProgressContinueCount ==
			m_State->m_StartNoProgressContinues,
			"A publication job returned Continue without progress.");
		require(ownership.m_PublicationRetainCount ==
			m_State->m_BaselineOwnership.m_PublicationRetainCount,
			"Publication retains did not return to the baseline.");
		require(!HasPendingIdentity(publication, m_State->m_Identity),
			"The terminal publication job is still queued.");

		if (successScenario)
		{
			require(m_State->m_SawPublicationQueue,
				"The resumable publication job was never observed in the ready queue.");
			require(publication.m_CompletedCount == m_State->m_StartCompleted + 1,
				"The success scenario did not complete exactly one publication job.");
			require(publication.m_ContinueCount > m_State->m_StartContinue + 1,
				"The model publication did not yield across multiple steps.");
			require(m_State->m_FramesWithPublicationSteps > 1,
				"The one-step budget did not spread publication across multiple frames.");
		}
		else
		{
			require(publication.m_FaultInjectionCount ==
				m_State->m_StartFaultInjections + 1,
				"The configured publication fault did not trigger exactly once.");
			if (action == AssetResourcePublicationFaultAction::Fail)
			{
				require(publication.m_FailedCount == m_State->m_StartFailed + 1,
					"The injected failure did not terminate the publication as Failed.");
			}
			else
			{
				require(publication.m_CancelledCount ==
					m_State->m_StartCancelled + 1,
					"The injected cancellation did not terminate the publication as Cancelled.");
			}
			require(ownership.m_LeaseCount ==
				m_State->m_BaselineOwnership.m_LeaseCount + 1,
				"Temporary dependency leases leaked after rollback.");
		}

		m_State->m_Passed = m_State->m_Errors.empty();
		m_State->m_Finished = true;
		if (m_State->m_Passed)
		{
			GGLAB_LOG_INFO(
				"Asset Publication Lab ({}) PASS: framesWithSteps={}, processedDelta={}.",
				ScenarioText(m_State->m_Scenario),
				m_State->m_FramesWithPublicationSteps,
				publication.m_ProcessedCount - m_State->m_StartProcessed);
		}
		else
		{
			GGLAB_LOG_ERROR(
				"Asset Publication Lab ({}) FAIL with {} invariant errors.",
				ScenarioText(m_State->m_Scenario),
				m_State->m_Errors.size());
			for (const std::string& error : m_State->m_Errors)
			{
				GGLAB_LOG_ERROR("Asset Publication Lab invariant: {}", error);
			}
		}
	}

	LabId AssetPublicationLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.asset_publication");
	}

	LabDescriptor AssetPublicationLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Asset Publication Lab",
			.m_Category = "Systems",
			.m_Description = "Deterministic one-step publication budgeting and stage fault injection for transaction rollback verification.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> AssetPublicationLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<AssetPublicationLabSession>(createInfo);
	}
}
