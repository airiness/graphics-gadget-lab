#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/AssetPublicationLabSession.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Asset/AssetManager.h"
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
			case AssetPublicationLabSession::Scenario::AcceptanceSuite:
				return "Asset Publication Acceptance Suite";
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return "Incremental Success";
			case AssetPublicationLabSession::Scenario::CancelTextures:
				return "Cancel During Textures";
			case AssetPublicationLabSession::Scenario::CancelMaterials:
				return "Cancel During Materials";
			case AssetPublicationLabSession::Scenario::CancelMeshes:
				return "Cancel During Meshes";
			case AssetPublicationLabSession::Scenario::CancelMeshInstances:
				return "Cancel During Mesh Instances";
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return "Cancel During Dependencies";
			case AssetPublicationLabSession::Scenario::CancelBeforeCommit:
				return "Cancel Before Commit";
			case AssetPublicationLabSession::Scenario::FailMaterials:
				return "Fail During Materials";
			}
			return "Unknown";
		}

		[[nodiscard]] std::filesystem::path ScenarioModelPath(
			AssetPublicationLabSession::Scenario scenario)
		{
			switch (scenario)
			{
			case AssetPublicationLabSession::Scenario::AcceptanceSuite:
				return {};
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return "Assets/Models/Sponza/Sponza.gltf";
			case AssetPublicationLabSession::Scenario::CancelTextures:
				return "Assets/Models/NormalTangentMirrorTest/NormalTangentMirrorTest.gltf";
			case AssetPublicationLabSession::Scenario::CancelMaterials:
				return "Assets/Models/MetalRoughSpheresNoTextures/MetalRoughSpheresNoTextures.gltf";
			case AssetPublicationLabSession::Scenario::CancelMeshes:
				return "Assets/Models/MetalRoughSpheres/MetalRoughSpheres.gltf";
			case AssetPublicationLabSession::Scenario::CancelMeshInstances:
				return "Assets/Models/TextureEncodingTest/TextureEncodingTest.gltf";
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return "Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf";
			case AssetPublicationLabSession::Scenario::CancelBeforeCommit:
				return "Assets/Models/TextureLinearInterpolationTest/TextureLinearInterpolationTest.gltf";
			case AssetPublicationLabSession::Scenario::FailMaterials:
				return "Assets/Models/FlightHelmet/FlightHelmet.gltf";
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
			case AssetPublicationLabSession::Scenario::CancelMaterials:
			case AssetPublicationLabSession::Scenario::FailMaterials:
				return AssetResourcePublicationStage::Materials;
			case AssetPublicationLabSession::Scenario::CancelMeshes:
				return AssetResourcePublicationStage::Meshes;
			case AssetPublicationLabSession::Scenario::CancelMeshInstances:
				return AssetResourcePublicationStage::MeshInstances;
			case AssetPublicationLabSession::Scenario::CancelDependencies:
				return AssetResourcePublicationStage::Dependencies;
			case AssetPublicationLabSession::Scenario::CancelBeforeCommit:
				return AssetResourcePublicationStage::Commit;
			case AssetPublicationLabSession::Scenario::AcceptanceSuite:
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
				return AssetResourcePublicationFaultAction::Fail;
			case AssetPublicationLabSession::Scenario::CancelTextures:
			case AssetPublicationLabSession::Scenario::CancelMaterials:
			case AssetPublicationLabSession::Scenario::CancelMeshes:
			case AssetPublicationLabSession::Scenario::CancelMeshInstances:
			case AssetPublicationLabSession::Scenario::CancelDependencies:
			case AssetPublicationLabSession::Scenario::CancelBeforeCommit:
				return AssetResourcePublicationFaultAction::Cancel;
			case AssetPublicationLabSession::Scenario::AcceptanceSuite:
			case AssetPublicationLabSession::Scenario::IncrementalSuccess:
				return AssetResourcePublicationFaultAction::None;
			}
			return AssetResourcePublicationFaultAction::None;
		}

		[[nodiscard]] bool HasPendingIdentity(const AssetStreamingQueueStatistics& queue,
			const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(queue.m_PendingWork,
				[identity](const AssetStreamingWorkActivity& work) noexcept
				{ return work.m_Identity == identity; });
		}

		[[nodiscard]] bool IsStreamingIdle(const AssetUploadStatistics& statistics) noexcept
		{
			return statistics.m_CpuPayloadQueue.m_PendingCount == 0 &&
				statistics.m_ResourcePublicationQueue.m_PendingCount == 0 &&
				statistics.m_UploadRecordingQueue.m_PendingCount == 0 &&
				statistics.m_GpuFinalizeQueue.m_PendingCount == 0 &&
				statistics.m_PendingCount == 0 && statistics.m_ReadyPayloadBytes == 0 &&
				statistics.m_InFlightBytes == 0;
		}

		[[nodiscard]] const AssetSnapshot::Texture* FindTextureSnapshot(
			const AssetSnapshot& snapshot, TextureID textureId) noexcept
		{
			const auto texture =
				std::ranges::find(snapshot.m_Textures, textureId, &AssetSnapshot::Texture::m_Id);
			return texture != snapshot.m_Textures.end() ? &*texture : nullptr;
		}

		[[nodiscard]] bool HasPendingUpload(const AssetUploadStatistics& statistics,
			const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(statistics.m_PendingUploads,
				[identity](const AssetUploadActivity& upload) noexcept
				{ return upload.m_Identity == identity; });
		}

		struct SyntheticPublicationState
		{
			uint32_t m_TargetSteps = 1;
			uint32_t m_Steps = 0;
			uint64_t m_ProgressToken = 0;
			uint32_t m_AbortCount = 0;
			AssetResourcePublicationAbortReason m_AbortReason =
				AssetResourcePublicationAbortReason::Shutdown;
			bool m_Completed = false;
		};

		class SyntheticPublicationJob final : public IResourcePublicationJob
		{
		public:
			explicit SyntheticPublicationJob(
				std::shared_ptr<SyntheticPublicationState> state) noexcept :
				m_State(std::move(state))
			{
			}

			[[nodiscard]] AssetResourcePublicationStepResult Step(
				AssetResourcePublicationContext& context) noexcept override
			{
				GGLAB_UNUSED(context);
				++m_State->m_Steps;
				++m_State->m_ProgressToken;
				if (m_State->m_Steps >= m_State->m_TargetSteps)
				{
					m_State->m_Completed = true;
					return {
						.m_Status = AssetResourcePublicationStepStatus::Completed,
						.m_Usage = {.m_Stage = AssetResourcePublicationStage::Textures},
					};
				}
				return {
					.m_Status = AssetResourcePublicationStepStatus::Continue,
					.m_Usage = {.m_Stage = AssetResourcePublicationStage::Textures},
				};
			}

			void Abort(AssetResourcePublicationContext& context,
				AssetResourcePublicationAbortReason reason) noexcept override
			{
				GGLAB_UNUSED(context);
				++m_State->m_AbortCount;
				m_State->m_AbortReason = reason;
			}

			[[nodiscard]] uint64_t GetProgressToken() const noexcept override
			{
				return m_State->m_ProgressToken;
			}

			[[nodiscard]] AssetResourcePublicationStage GetCurrentStage() const noexcept override
			{
				return AssetResourcePublicationStage::Textures;
			}

		private:
			std::shared_ptr<SyntheticPublicationState> m_State;
		};
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
		bool m_HalfPublishedModelObserved = false;
		bool m_Finished = false;
		bool m_Passed = false;
		bool m_TimedOut = false;
		std::vector<std::string> m_Errors;
	};

	struct AssetPublicationLabSession::AcceptanceSuiteState
	{
		enum class Case : uint8_t
		{
			IncrementalSuccess,
			CancelTextures,
			CancelMaterials,
			CancelMeshes,
			CancelMeshInstances,
			CancelDependencies,
			CancelBeforeCommit,
			FailMaterials,
			StaleGeneration,
			SharedTextureRollback,
			OwnershipPriorityMerge,
			GpuSubmittedCancellation,
			ShutdownDrain,
			Count,
		};

		enum class Phase : uint8_t
		{
			Starting,
			RunningModel,
			WaitingOldYield,
			WaitingNewGeneration,
			WaitingSharedTexture,
			WaitingSharedRollback,
			WaitingGpuSubmission,
			WaitingGpuCancellation,
			Completed,
		};

		struct Result
		{
			std::string m_Name;
			bool m_Passed = false;
			std::vector<std::string> m_Errors;
		};

		Case m_Case = Case::IncrementalSuccess;
		Phase m_Phase = Phase::Starting;
		AssetOwnershipStatistics m_BaselineOwnership{};
		AssetUploadStatistics m_BaselineUpload{};
		AssetOwnershipStatistics m_CaseBaselineOwnership{};
		AssetUploadStatistics m_CaseBaselineUpload{};
		AssetOwnerScope m_PrimaryOwner;
		AssetOwnerScope m_SecondaryOwner;
		AssetManager::TextureLoadRequest m_TextureRequest{};
		AssetManager::ModelLoadRequest m_ModelRequest{};
		AssetStreamingIdentity m_TextureIdentity{};
		AssetStreamingIdentity m_ModelIdentity{};
		std::shared_ptr<SyntheticPublicationState> m_OldGeneration;
		std::shared_ptr<SyntheticPublicationState> m_NewGeneration;
		std::shared_ptr<SyntheticPublicationState> m_DrainJob;
		AssetStreamingIdentity m_OldIdentity{};
		AssetStreamingIdentity m_NewIdentity{};
		uint32_t m_OldStepsAtCancellation = 0;
		uint64_t m_StartGpuDeferredCancellations = 0;
		uint64_t m_StartNoProgressContinues = 0;
		float m_CaseElapsedSeconds = 0.0f;
		float m_TotalElapsedSeconds = 0.0f;
		uint32_t m_SettleFrames = 0;
		bool m_GpuHoldObserved = false;
		bool m_Finished = false;
		bool m_Passed = false;
		std::vector<Result> m_Results;
		std::vector<std::string> m_FinalErrors;
	};

	AssetPublicationLabSession::AssetPublicationLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ScenarioId,
			.m_Name = "Scenario",
			.m_Group = "Verification",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(0),
			.m_EnumItems =
				{
					{.m_Value = 0, .m_Name = "Asset Publication Acceptance Suite"},
					{.m_Value = 1, .m_Name = "Incremental Success"},
					{.m_Value = 2, .m_Name = "Cancel During Textures"},
					{.m_Value = 3, .m_Name = "Cancel During Materials"},
					{.m_Value = 4, .m_Name = "Cancel During Meshes"},
					{.m_Value = 5, .m_Name = "Cancel During Mesh Instances"},
					{.m_Value = 6, .m_Name = "Cancel During Dependencies"},
					{.m_Value = 7, .m_Name = "Cancel Before Commit"},
					{.m_Value = 8, .m_Name = "Fail During Materials"},
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
		if (m_Suite)
		{
			UpdateAcceptanceSuite(deltaTime);
			return;
		}
		UpdateModelScenario(deltaTime);
	}

	void AssetPublicationLabSession::UpdateModelScenario(float deltaTime) noexcept
	{
		if (!m_State || m_State->m_Finished)
		{
			return;
		}

		m_State->m_ElapsedSeconds += deltaTime;
		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		const AssetUploadStatistics statistics = scheduler->GetStatistics();
		const uint64_t processed = statistics.m_ResourcePublicationQueue.m_ProcessedCount;
		if (processed > m_State->m_LastProcessed)
		{
			++m_State->m_FramesWithPublicationSteps;
			m_State->m_LastProcessed = processed;
		}
		m_State->m_SawPublicationQueue |=
			HasPendingIdentity(statistics.m_ResourcePublicationQueue, m_State->m_Identity);

		const Model* model = m_Services.m_AssetManager->GetModel(m_State->m_Request.m_ModelId);
		if (model && model->m_ContentGeneration == m_State->m_Request.m_Generation)
		{
			m_State->m_FinalModelState = model->m_State;
			if (model->m_State == AssetState::Publishing && !model->m_MeshInstance.empty())
			{
				m_State->m_HalfPublishedModelObserved = true;
			}
			const bool terminal = model->m_State == AssetState::Ready ||
				model->m_State == AssetState::Failed ||
				model->m_State == AssetState::Cancelled;
			if (terminal && IsStreamingIdle(statistics) &&
				!HasPendingIdentity(statistics.m_ResourcePublicationQueue, m_State->m_Identity))
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
		if (m_Suite)
		{
			diagnostics.m_Metrics = {
				{.m_Name = "Scenario", .m_Value = "Asset Publication Acceptance Suite"},
				{.m_Name = "Completed cases",
					.m_Value = std::format("{} / {}", m_Suite->m_Results.size(),
						static_cast<size_t>(AcceptanceSuiteState::Case::Count))},
				{.m_Name = "Elapsed",
					.m_Value = std::format("{:.2f} s", m_Suite->m_TotalElapsedSeconds)},
			};
			diagnostics.m_Checks.push_back({
				.m_Name = "Acceptance suite",
				.m_Status = !m_Suite->m_Finished ? LabDiagnosticCheckStatus::Pending
							: m_Suite->m_Passed ? LabDiagnosticCheckStatus::Passed
												 : LabDiagnosticCheckStatus::Failed,
				.m_Detail = !m_Suite->m_Finished ? "Acceptance cases are running."
							: m_Suite->m_Passed
								? "All asset publication acceptance cases passed."
								: "One or more asset publication acceptance cases failed.",
				});
			for (const AcceptanceSuiteState::Result& result : m_Suite->m_Results)
			{
				diagnostics.m_Checks.push_back({
					.m_Name = result.m_Name,
					.m_Status = result.m_Passed ? LabDiagnosticCheckStatus::Passed
												: LabDiagnosticCheckStatus::Failed,
					.m_Detail = result.m_Passed
									? "Passed."
									: std::format("{} invariant errors.", result.m_Errors.size()),
					});
			}
			for (const std::string& error : m_Suite->m_FinalErrors)
			{
				diagnostics.m_Checks.push_back({
					.m_Name = "Suite invariant",
					.m_Status = LabDiagnosticCheckStatus::Failed,
					.m_Detail = error,
					});
			}
			return;
		}
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
			{.m_Name = "Scenario", .m_Value = ScenarioText(m_State->m_Scenario)},
			{.m_Name = "Model", .m_Value = m_State->m_ModelPath.generic_string()},
			{.m_Name = "Model ID / generation",
				.m_Value = std::format("{} / {}", m_State->m_Request.m_ModelId.Value(),
					m_State->m_Request.m_Generation)},
			{.m_Name = "Frames with publication steps",
				.m_Value = std::to_string(m_State->m_FramesWithPublicationSteps)},
			{.m_Name = "Active publication retains",
				.m_Value = std::to_string(ownership.m_PublicationRetainCount)},
			{.m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_ElapsedSeconds)},
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "Scenario result",
			.m_Status = !m_State->m_Finished ? LabDiagnosticCheckStatus::Pending
						: m_State->m_Passed ? LabDiagnosticCheckStatus::Passed
											 : LabDiagnosticCheckStatus::Failed,
			.m_Detail = !m_State->m_Finished ? "Scenario is running."
						: m_State->m_Passed ? "All publication invariants passed."
											 : "One or more publication invariants failed.",
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
		m_Scenario = static_cast<Scenario>(GetParameters().Get(ScenarioId, int32_t(0)));
		m_FaultOccurrence = GetParameters().Get(FaultOccurrenceId, uint32_t(1));
		if (m_Entered)
		{
			StartScenario();
		}
	}

	void AssetPublicationLabSession::StartScenario() noexcept
	{
		StopScenario();
		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
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
		if (m_Scenario == Scenario::AcceptanceSuite)
		{
			StartAcceptanceSuite();
			return;
		}
		StartModelScenario(m_Scenario, m_FaultOccurrence);
	}

	void AssetPublicationLabSession::StartModelScenario(
		Scenario scenario, uint32_t faultOccurrence) noexcept
	{
		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		scheduler->ClearResourcePublicationFault();
		ResetAssetInterests();
		m_State = std::make_unique<ScenarioState>();
		m_State->m_Scenario = scenario;
		m_State->m_ModelPath = ScenarioModelPath(scenario);
		m_State->m_BaselineOwnership = m_Services.m_AssetManager->GetOwnershipStatistics();
		const AssetUploadStatistics before = scheduler->GetStatistics();
		const auto& publicationBefore = before.m_ResourcePublicationQueue;
		m_State->m_StartEnqueued = publicationBefore.m_EnqueuedCount;
		m_State->m_StartProcessed = publicationBefore.m_ProcessedCount;
		m_State->m_StartContinue = publicationBefore.m_ContinueCount;
		m_State->m_StartCompleted = publicationBefore.m_CompletedCount;
		m_State->m_StartFailed = publicationBefore.m_FailedCount;
		m_State->m_StartCancelled = publicationBefore.m_CancelledCount;
		m_State->m_StartFaultInjections = publicationBefore.m_FaultInjectionCount;
		m_State->m_StartNoProgressContinues = publicationBefore.m_NoProgressContinueCount;
		m_State->m_LastProcessed = publicationBefore.m_ProcessedCount;

		m_State->m_Request =
			GetAssetOwnerScope().LoadModelAsync(m_State->m_ModelPath, TaskPriority::Normal);
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

		const AssetResourcePublicationFaultAction action = FaultAction(scenario);
		if (action != AssetResourcePublicationFaultAction::None)
		{
			scheduler->ArmResourcePublicationFault({
				.m_Identity = m_State->m_Identity,
				.m_Stage = FaultStage(scenario),
				.m_Action = action,
				.m_Timing = scenario == Scenario::CancelBeforeCommit
								? AssetResourcePublicationFaultTiming::BeforeStep
								: AssetResourcePublicationFaultTiming::AfterStep,
				.m_TriggerOccurrence = faultOccurrence,
				});
		}
	}

	void AssetPublicationLabSession::StopScenario() noexcept
	{
		if (m_Services.m_Renderer)
		{
			AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
			scheduler->ClearResourcePublicationFault();
			scheduler->ClearGpuCompletionHold();
			if (m_HasOriginalBudget)
			{
				scheduler->SetFrameBudget(m_OriginalBudget);
			}
		}
		ResetAssetInterests();
		m_State.reset();
		m_Suite.reset();
	}

	void AssetPublicationLabSession::EvaluateScenario(
		const AssetUploadStatistics& statistics) noexcept
	{
		GGLAB_ASSERT(m_State);
		const auto& publication = statistics.m_ResourcePublicationQueue;
		const AssetOwnershipStatistics ownership =
			m_Services.m_AssetManager->GetOwnershipStatistics();
		const bool successScenario = m_State->m_Scenario == Scenario::IncrementalSuccess;
		const AssetResourcePublicationFaultAction action = FaultAction(m_State->m_Scenario);
		const AssetState expectedState = successScenario ? AssetState::Ready
			: action == AssetResourcePublicationFaultAction::Fail
			? AssetState::Failed
			: AssetState::Cancelled;

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
		require(publication.m_NoProgressContinueCount == m_State->m_StartNoProgressContinues,
			"A publication job returned Continue without progress.");
		require(ownership.m_PublicationRetainCount ==
			m_State->m_BaselineOwnership.m_PublicationRetainCount,
			"Publication retains did not return to the baseline.");
		require(!HasPendingIdentity(publication, m_State->m_Identity),
			"The terminal publication job is still queued.");
		require(!m_State->m_HalfPublishedModelObserved,
			"A model exposed mesh instances while still in Publishing state.");
		require(IsStreamingIdle(statistics),
			"Streaming queues or in-flight staging did not settle after the scenario.");

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
			require(publication.m_FaultInjectionCount == m_State->m_StartFaultInjections + 1,
				"The configured publication fault did not trigger exactly once.");
			if (action == AssetResourcePublicationFaultAction::Fail)
			{
				require(publication.m_FailedCount == m_State->m_StartFailed + 1,
					"The injected failure did not terminate the publication as Failed.");
			}
			else
			{
				require(publication.m_CancelledCount == m_State->m_StartCancelled + 1,
					"The injected cancellation did not terminate the publication as Cancelled.");
			}
			require(ownership.m_LeaseCount == m_State->m_BaselineOwnership.m_LeaseCount + 1,
				"Temporary dependency leases leaked after rollback.");
		}

		m_State->m_Passed = m_State->m_Errors.empty();
		m_State->m_Finished = true;
		if (m_State->m_Passed)
		{
			GGLAB_LOG_INFO(
				"Asset Publication Lab ({}) PASS: framesWithSteps={}, processedDelta={}.",
				ScenarioText(m_State->m_Scenario), m_State->m_FramesWithPublicationSteps,
				publication.m_ProcessedCount - m_State->m_StartProcessed);
		}
		else
		{
			GGLAB_LOG_ERROR("Asset Publication Lab ({}) FAIL with {} invariant errors.",
				ScenarioText(m_State->m_Scenario), m_State->m_Errors.size());
			for (const std::string& error : m_State->m_Errors)
			{
				GGLAB_LOG_ERROR("Asset Publication Lab invariant: {}", error);
			}
		}
	}

	void AssetPublicationLabSession::StartAcceptanceSuite() noexcept
	{
		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		m_Suite = std::make_unique<AcceptanceSuiteState>();
		m_Suite->m_BaselineOwnership = m_Services.m_AssetManager->GetOwnershipStatistics();
		m_Suite->m_BaselineUpload = scheduler->GetStatistics();
		StartAcceptanceCase();
	}

	void AssetPublicationLabSession::StartAcceptanceCase() noexcept
	{
		GGLAB_ASSERT(m_Suite);
		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		AssetManager* assetManager = m_Services.m_AssetManager;
		scheduler->ClearResourcePublicationFault();
		scheduler->ClearGpuCompletionHold();
		m_Suite->m_CaseBaselineOwnership = assetManager->GetOwnershipStatistics();
		m_Suite->m_CaseBaselineUpload = scheduler->GetStatistics();
		m_Suite->m_CaseElapsedSeconds = 0.0f;
		m_Suite->m_SettleFrames = 0;
		m_Suite->m_GpuHoldObserved = false;

		const auto startModel = [this](Scenario scenario, uint32_t occurrence) noexcept
			{
				StartModelScenario(scenario, occurrence);
				m_Suite->m_Phase = AcceptanceSuiteState::Phase::RunningModel;
			};
		switch (m_Suite->m_Case)
		{
		case AcceptanceSuiteState::Case::IncrementalSuccess:
			startModel(Scenario::IncrementalSuccess, 1);
			break;
		case AcceptanceSuiteState::Case::CancelTextures:
			startModel(Scenario::CancelTextures, 2);
			break;
		case AcceptanceSuiteState::Case::CancelMaterials:
			startModel(Scenario::CancelMaterials, 32);
			break;
		case AcceptanceSuiteState::Case::CancelMeshes:
			startModel(Scenario::CancelMeshes, 1);
			break;
		case AcceptanceSuiteState::Case::CancelMeshInstances:
			startModel(Scenario::CancelMeshInstances, 7);
			break;
		case AcceptanceSuiteState::Case::CancelDependencies:
			startModel(Scenario::CancelDependencies, 6);
			break;
		case AcceptanceSuiteState::Case::CancelBeforeCommit:
			startModel(Scenario::CancelBeforeCommit, 1);
			break;
		case AcceptanceSuiteState::Case::FailMaterials:
			startModel(Scenario::FailMaterials, 3);
			break;
		case AcceptanceSuiteState::Case::StaleGeneration:
		{
			constexpr uint64_t StableId = std::numeric_limits<uint64_t>::max() - 1024;
			m_Suite->m_OldIdentity = {
				.m_Kind = AssetStreamingWorkKind::Model,
				.m_StableId = StableId,
				.m_Generation = 1,
			};
			m_Suite->m_NewIdentity = {
				.m_Kind = AssetStreamingWorkKind::Model,
				.m_StableId = StableId,
				.m_Generation = 2,
			};
			m_Suite->m_OldGeneration = std::make_shared<SyntheticPublicationState>();
			m_Suite->m_OldGeneration->m_TargetSteps = 8;
			m_Suite->m_NewGeneration = std::make_shared<SyntheticPublicationState>();
			m_Suite->m_NewGeneration->m_TargetSteps = 3;
			m_Suite->m_StartNoProgressContinues =
				m_Suite->m_CaseBaselineUpload.m_ResourcePublicationQueue.m_NoProgressContinueCount;
			scheduler->EnqueueResourcePublication(
				{
					.m_Name = "Acceptance stale generation 1",
					.m_Identity = m_Suite->m_OldIdentity,
					.m_Priority = TaskPriority::Normal,
				},
				std::make_unique<SyntheticPublicationJob>(m_Suite->m_OldGeneration));
			m_Suite->m_Phase = AcceptanceSuiteState::Phase::WaitingOldYield;
			break;
		}
		case AcceptanceSuiteState::Case::SharedTextureRollback:
		{
			m_Suite->m_PrimaryOwner = assetManager->CreateOwnerScope();
			m_Suite->m_TextureRequest = m_Suite->m_PrimaryOwner.LoadTextureAsync(
				"Assets/Models/NormalTangentTest/NormalTangentTest_BaseColor.png",
				TextureSemantic::BaseColor, TaskPriority::Normal);
			if (!m_Suite->m_TextureRequest.IsValid())
			{
				CompleteAcceptanceCase("Shared Texture rollback",
					{ "AssetManager rejected the shared texture request." });
				return;
			}
			m_Suite->m_TextureIdentity = {
				.m_Kind = AssetStreamingWorkKind::Texture,
				.m_StableId = m_Suite->m_TextureRequest.m_TextureId.Value(),
				.m_Generation = m_Suite->m_TextureRequest.m_Generation,
			};
			m_Suite->m_Phase = AcceptanceSuiteState::Phase::WaitingSharedTexture;
			break;
		}
		case AcceptanceSuiteState::Case::OwnershipPriorityMerge:
		{
			m_Suite->m_PrimaryOwner = assetManager->CreateOwnerScope();
			m_Suite->m_SecondaryOwner = assetManager->CreateOwnerScope();
			const AssetManager::TextureLoadRequest backgroundRequest =
				m_Suite->m_PrimaryOwner.LoadTextureAsync(
					"Assets/Models/NormalTangentTest/NormalTangentTest_BaseColor.png",
					TextureSemantic::BaseColor, TaskPriority::Background);
			const AssetManager::TextureLoadRequest criticalRequest =
				m_Suite->m_SecondaryOwner.LoadTextureAsync(
					"Assets/Models/NormalTangentTest/NormalTangentTest_BaseColor.png",
					TextureSemantic::BaseColor, TaskPriority::Critical);
			std::vector<std::string> errors;
			if (!backgroundRequest.IsValid() || !criticalRequest.IsValid() ||
				backgroundRequest.m_TextureId != criticalRequest.m_TextureId ||
				backgroundRequest.m_Generation != criticalRequest.m_Generation)
			{
				errors.push_back("Two owners did not acquire the same texture content version.");
			}
			else
			{
				const auto findInterest = [id = backgroundRequest.m_TextureId](
					const AssetOwnershipStatistics& ownership) noexcept
					-> const AssetInterestActivity*
					{
						const auto interest = std::ranges::find_if(ownership.m_ActiveInterests,
							[id](const AssetInterestActivity& activity) noexcept
							{
								return activity.m_Kind == AssetKind::Texture &&
									activity.m_StableId == id.Value();
							});
						return interest != ownership.m_ActiveInterests.end() ? &*interest : nullptr;
					};

				const AssetOwnershipStatistics merged = assetManager->GetOwnershipStatistics();
				const AssetInterestActivity* mergedInterest = findInterest(merged);
				if (!mergedInterest ||
					mergedInterest->m_Generation != backgroundRequest.m_Generation ||
					mergedInterest->m_LeaseCount != 2 || mergedInterest->m_OwnerCount != 2 ||
					mergedInterest->m_EffectivePriority != TaskPriority::Critical)
				{
					errors.push_back(
						"Multiple leases did not merge to the highest owner priority.");
				}

				m_Suite->m_SecondaryOwner.Reset();
				const AssetOwnershipStatistics reduced = assetManager->GetOwnershipStatistics();
				const AssetInterestActivity* reducedInterest = findInterest(reduced);
				if (!reducedInterest ||
					reducedInterest->m_Generation != backgroundRequest.m_Generation ||
					reducedInterest->m_LeaseCount != 1 || reducedInterest->m_OwnerCount != 1 ||
					reducedInterest->m_EffectivePriority != TaskPriority::Background)
				{
					errors.push_back(
						"Releasing the critical lease did not restore background priority.");
				}
				if (reduced.m_PriorityUpdateCount <
					m_Suite->m_CaseBaselineOwnership.m_PriorityUpdateCount + 2)
				{
					errors.push_back("Priority merge and release were not both recorded.");
				}
			}
			CompleteAcceptanceCase("Ownership priority merge", std::move(errors));
			return;
		}
		case AcceptanceSuiteState::Case::GpuSubmittedCancellation:
		{
			m_Suite->m_PrimaryOwner = assetManager->CreateOwnerScope();
			m_Suite->m_TextureRequest = m_Suite->m_PrimaryOwner.LoadTextureAsync(
				"Assets/Models/FlightHelmet/FlightHelmet_Materials_LensesMat_BaseColor.png",
				TextureSemantic::BaseColor, TaskPriority::Normal);
			if (!m_Suite->m_TextureRequest.IsValid())
			{
				CompleteAcceptanceCase("GPU submitted cancellation",
					{ "AssetManager rejected the GPU cancellation texture request." });
				return;
			}
			m_Suite->m_TextureIdentity = {
				.m_Kind = AssetStreamingWorkKind::Texture,
				.m_StableId = m_Suite->m_TextureRequest.m_TextureId.Value(),
				.m_Generation = m_Suite->m_TextureRequest.m_Generation,
			};
			m_Suite->m_StartGpuDeferredCancellations =
				m_Suite->m_CaseBaselineOwnership.m_GpuDeferredCancellationCount;
			scheduler->ArmGpuCompletionHold(m_Suite->m_TextureIdentity);
			m_Suite->m_Phase = AcceptanceSuiteState::Phase::WaitingGpuSubmission;
			break;
		}
		case AcceptanceSuiteState::Case::ShutdownDrain:
		{
			m_Suite->m_DrainJob = std::make_shared<SyntheticPublicationState>();
			m_Suite->m_DrainJob->m_TargetSteps = 64;
			const AssetStreamingIdentity identity{
				.m_Kind = AssetStreamingWorkKind::Model,
				.m_StableId = std::numeric_limits<uint64_t>::max() - 2048,
				.m_Generation = 1,
			};
			m_Suite->m_StartNoProgressContinues =
				m_Suite->m_CaseBaselineUpload.m_ResourcePublicationQueue.m_NoProgressContinueCount;
			scheduler->EnqueueResourcePublication(
				{
					.m_Name = "Acceptance shutdown drain",
					.m_Identity = identity,
					.m_Priority = TaskPriority::Normal,
				},
				std::make_unique<SyntheticPublicationJob>(m_Suite->m_DrainJob));
			scheduler->DrainReadyWork();
			const AssetUploadStatistics after = scheduler->GetStatistics();
			std::vector<std::string> errors;
			if (!m_Suite->m_DrainJob->m_Completed ||
				m_Suite->m_DrainJob->m_Steps != m_Suite->m_DrainJob->m_TargetSteps)
			{
				errors.push_back("DrainReadyWork did not complete every yielded synthetic step.");
			}
			if (m_Suite->m_DrainJob->m_AbortCount != 0)
			{
				errors.push_back("DrainReadyWork aborted a finite-progress publication job.");
			}
			if (HasPendingIdentity(after.m_ResourcePublicationQueue, identity))
			{
				errors.push_back("The drained publication job remains queued.");
			}
			if (after.m_ResourcePublicationQueue.m_NoProgressContinueCount !=
				m_Suite->m_StartNoProgressContinues)
			{
				errors.push_back("DrainReadyWork observed a no-progress Continue.");
			}
			CompleteAcceptanceCase("Shutdown drain", std::move(errors));
			return;
		}
		case AcceptanceSuiteState::Case::Count:
			CompleteAcceptanceSuite();
			break;
		}
	}

	void AssetPublicationLabSession::UpdateAcceptanceSuite(float deltaTime) noexcept
	{
		GGLAB_ASSERT(m_Suite);
		if (m_Suite->m_Finished)
		{
			return;
		}
		m_Suite->m_TotalElapsedSeconds += deltaTime;
		m_Suite->m_CaseElapsedSeconds += deltaTime;
		if (m_Suite->m_TotalElapsedSeconds > 600.0f || m_Suite->m_CaseElapsedSeconds > 120.0f)
		{
			CompleteAcceptanceCase("Timed out acceptance case",
				{ "The acceptance case exceeded its deterministic timeout." });
			return;
		}

		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		AssetManager* assetManager = m_Services.m_AssetManager;
		const AssetUploadStatistics statistics = scheduler->GetStatistics();
		switch (m_Suite->m_Phase)
		{
		case AcceptanceSuiteState::Phase::RunningModel:
			UpdateModelScenario(deltaTime);
			if (m_State && m_State->m_Finished)
			{
				CompleteAcceptanceCase(ScenarioText(m_State->m_Scenario), m_State->m_Errors);
			}
			break;

		case AcceptanceSuiteState::Phase::WaitingOldYield:
			if (m_Suite->m_OldGeneration->m_Steps > 0)
			{
				scheduler->EnqueueResourcePublication(
					{
						.m_Name = "Acceptance current generation 2",
						.m_Identity = m_Suite->m_NewIdentity,
						.m_Priority = TaskPriority::Normal,
					},
					std::make_unique<SyntheticPublicationJob>(m_Suite->m_NewGeneration));
				const uint32_t cancelled = scheduler->CancelReadyWork(m_Suite->m_OldIdentity);
				m_Suite->m_OldStepsAtCancellation = m_Suite->m_OldGeneration->m_Steps;
				if (cancelled != 1)
				{
					CompleteAcceptanceCase("Stale generation rejection",
						{ std::format(
							"Expected one stale job cancellation, observed {}.", cancelled) });
					return;
				}
				m_Suite->m_Phase = AcceptanceSuiteState::Phase::WaitingNewGeneration;
			}
			break;

		case AcceptanceSuiteState::Phase::WaitingNewGeneration:
			if (m_Suite->m_NewGeneration->m_Completed &&
				!HasPendingIdentity(
					statistics.m_ResourcePublicationQueue, m_Suite->m_NewIdentity) &&
				!HasPendingIdentity(statistics.m_ResourcePublicationQueue, m_Suite->m_OldIdentity))
			{
				std::vector<std::string> errors;
				if (m_Suite->m_OldGeneration->m_AbortCount != 1 ||
					m_Suite->m_OldGeneration->m_AbortReason !=
					AssetResourcePublicationAbortReason::Cancelled)
				{
					errors.push_back(
						"The stale generation was not aborted exactly once as Cancelled.");
				}
				if (m_Suite->m_OldGeneration->m_Steps != m_Suite->m_OldStepsAtCancellation)
				{
					errors.push_back("The stale generation executed again after cancellation.");
				}
				if (m_Suite->m_NewGeneration->m_AbortCount != 0 ||
					m_Suite->m_NewGeneration->m_Steps != m_Suite->m_NewGeneration->m_TargetSteps)
				{
					errors.push_back("The current generation did not complete independently.");
				}
				if (statistics.m_ResourcePublicationQueue.m_NoProgressContinueCount !=
					m_Suite->m_StartNoProgressContinues)
				{
					errors.push_back("The generation test produced a no-progress Continue.");
				}
				CompleteAcceptanceCase("Stale generation rejection", std::move(errors));
			}
			break;

		case AcceptanceSuiteState::Phase::WaitingSharedTexture:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(*assetManager);
			const AssetSnapshot::Texture* texture =
				FindTextureSnapshot(snapshot, m_Suite->m_TextureRequest.m_TextureId);
			if (texture && texture->m_State == AssetState::Ready && texture->m_IsUploaded)
			{
				m_Suite->m_SecondaryOwner = assetManager->CreateOwnerScope();
				m_Suite->m_ModelRequest = m_Suite->m_SecondaryOwner.LoadModelAsync(
					"Assets/Models/NormalTangentTest/NormalTangentTest.gltf", TaskPriority::Normal);
				if (!m_Suite->m_ModelRequest.IsValid())
				{
					CompleteAcceptanceCase("Shared Texture rollback",
						{ "AssetManager rejected the rollback model request." });
					return;
				}
				m_Suite->m_ModelIdentity = {
					.m_Kind = AssetStreamingWorkKind::Model,
					.m_StableId = m_Suite->m_ModelRequest.m_ModelId.Value(),
					.m_Generation = m_Suite->m_ModelRequest.m_Generation,
				};
				scheduler->ArmResourcePublicationFault({
					.m_Identity = m_Suite->m_ModelIdentity,
					.m_Stage = AssetResourcePublicationStage::Materials,
					.m_Action = AssetResourcePublicationFaultAction::Fail,
					.m_TriggerOccurrence = 1,
					});
				m_Suite->m_Phase = AcceptanceSuiteState::Phase::WaitingSharedRollback;
			}
			else if (texture && (texture->m_State == AssetState::Failed ||
				texture->m_State == AssetState::Cancelled))
			{
				CompleteAcceptanceCase("Shared Texture rollback",
					{ "The shared texture failed before the rollback model started." });
			}
			break;
		}

		case AcceptanceSuiteState::Phase::WaitingSharedRollback:
		{
			const Model* model = assetManager->GetModel(m_Suite->m_ModelRequest.m_ModelId);
			if (model && model->m_ContentGeneration == m_Suite->m_ModelRequest.m_Generation &&
				model->m_State == AssetState::Failed && IsStreamingIdle(statistics))
			{
				if (++m_Suite->m_SettleFrames < 4)
				{
					break;
				}
				std::vector<std::string> errors;
				const AssetSnapshot snapshot = BuildAssetSnapshot(*assetManager);
				const AssetSnapshot::Texture* texture =
					FindTextureSnapshot(snapshot, m_Suite->m_TextureRequest.m_TextureId);
				if (!texture ||
					texture->m_ContentGeneration != m_Suite->m_TextureRequest.m_Generation ||
					texture->m_State != AssetState::Ready || !texture->m_IsUploaded ||
					!texture->m_Texture.IsValid())
				{
					errors.push_back("Rollback removed or invalidated the shared Ready texture.");
				}
				const AssetOwnershipStatistics ownership = assetManager->GetOwnershipStatistics();
				const bool sharedInterestAlive = std::ranges::any_of(ownership.m_ActiveInterests,
					[this](const AssetInterestActivity& interest) noexcept
					{
						return interest.m_Kind == AssetKind::Texture &&
							interest.m_StableId == m_Suite->m_TextureIdentity.m_StableId &&
							interest.m_Generation == m_Suite->m_TextureIdentity.m_Generation &&
							interest.m_OwnerCount >= 1;
					});
				if (!sharedInterestAlive)
				{
					errors.push_back(
						"The surviving shared texture lost its external owner interest.");
				}
				if (ownership.m_PublicationRetainCount !=
					m_Suite->m_CaseBaselineOwnership.m_PublicationRetainCount)
				{
					errors.push_back("Shared rollback leaked a publication retain.");
				}
				CompleteAcceptanceCase("Shared Texture rollback", std::move(errors));
			}
			break;
		}

		case AcceptanceSuiteState::Phase::WaitingGpuSubmission:
			if (HasPendingUpload(statistics, m_Suite->m_TextureIdentity))
			{
				std::vector<std::string> errors;
				const AssetSnapshot beforeCancellation = BuildAssetSnapshot(*assetManager);
				const AssetSnapshot::Texture* heldTexture =
					FindTextureSnapshot(beforeCancellation, m_Suite->m_TextureRequest.m_TextureId);
				m_Suite->m_GpuHoldObserved = heldTexture &&
					heldTexture->m_State == AssetState::GpuProcessing &&
					heldTexture->m_Texture.IsValid();
				if (!m_Suite->m_GpuHoldObserved)
				{
					errors.push_back(
						"The held upload was not observable as an allocated GPU resource.");
				}
				m_Suite->m_PrimaryOwner.Reset();
				const AssetSnapshot afterCancellation = BuildAssetSnapshot(*assetManager);
				const AssetSnapshot::Texture* cancelledTexture =
					FindTextureSnapshot(afterCancellation, m_Suite->m_TextureRequest.m_TextureId);
				if (!cancelledTexture || cancelledTexture->m_State != AssetState::GpuProcessing ||
					!cancelledTexture->m_Texture.IsValid())
				{
					errors.push_back(
						"Post-submit cancellation destroyed the texture before fence finalization.");
				}
				if (!errors.empty())
				{
					scheduler->ClearGpuCompletionHold();
					CompleteAcceptanceCase("GPU submitted cancellation", std::move(errors));
					return;
				}
				scheduler->ClearGpuCompletionHold();
				m_Suite->m_Phase = AcceptanceSuiteState::Phase::WaitingGpuCancellation;
			}
			break;

		case AcceptanceSuiteState::Phase::WaitingGpuCancellation:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(*assetManager);
			const AssetSnapshot::Texture* texture =
				FindTextureSnapshot(snapshot, m_Suite->m_TextureRequest.m_TextureId);
			if (texture && texture->m_State == AssetState::Cancelled &&
				!HasPendingUpload(statistics, m_Suite->m_TextureIdentity) &&
				IsStreamingIdle(statistics))
			{
				if (++m_Suite->m_SettleFrames < 4)
				{
					break;
				}
				std::vector<std::string> errors;
				if (texture->m_Texture.IsValid() || texture->m_IsUploaded)
				{
					errors.push_back("Cancelled GPU resources survived fence-safe finalization.");
				}
				const AssetOwnershipStatistics ownership = assetManager->GetOwnershipStatistics();
				if (ownership.m_GpuDeferredCancellationCount !=
					m_Suite->m_StartGpuDeferredCancellations + 1)
				{
					errors.push_back(
						"GPU deferred cancellation telemetry did not advance exactly once.");
				}
				if (!m_Suite->m_GpuHoldObserved)
				{
					errors.push_back("The test never observed an upload after submission.");
				}
				CompleteAcceptanceCase("GPU submitted cancellation", std::move(errors));
			}
			break;
		}

		case AcceptanceSuiteState::Phase::Starting:
		case AcceptanceSuiteState::Phase::Completed:
			break;
		}
	}

	void AssetPublicationLabSession::CompleteAcceptanceCase(
		std::string name, std::vector<std::string> errors) noexcept
	{
		GGLAB_ASSERT(m_Suite);
		const bool passed = errors.empty();
		if (passed)
		{
			GGLAB_LOG_INFO("Asset publication acceptance case PASS: {}.", name);
		}
		else
		{
			GGLAB_LOG_ERROR("Asset publication acceptance case FAIL: {} ({} invariant errors).",
				name, errors.size());
			for (const std::string& error : errors)
			{
				GGLAB_LOG_ERROR("Asset publication acceptance invariant: {}", error);
			}
		}
		m_Suite->m_Results.push_back({
			.m_Name = std::move(name),
			.m_Passed = passed,
			.m_Errors = std::move(errors),
			});

		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		scheduler->ClearResourcePublicationFault();
		scheduler->ClearGpuCompletionHold();
		ResetAssetInterests();
		m_State.reset();
		m_Suite->m_PrimaryOwner.Reset();
		m_Suite->m_SecondaryOwner.Reset();
		m_Suite->m_PrimaryOwner = {};
		m_Suite->m_SecondaryOwner = {};
		m_Suite->m_Phase = AcceptanceSuiteState::Phase::Starting;
		m_Suite->m_Case =
			static_cast<AcceptanceSuiteState::Case>(static_cast<uint8_t>(m_Suite->m_Case) + 1);
		if (m_Suite->m_Case == AcceptanceSuiteState::Case::Count)
		{
			CompleteAcceptanceSuite();
		}
		else
		{
			StartAcceptanceCase();
		}
	}

	void AssetPublicationLabSession::CompleteAcceptanceSuite() noexcept
	{
		GGLAB_ASSERT(m_Suite);
		AssetUploadScheduler* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		const AssetUploadStatistics statistics = scheduler->GetStatistics();
		const AssetOwnershipStatistics ownership =
			m_Services.m_AssetManager->GetOwnershipStatistics();
		const auto& before = m_Suite->m_BaselineUpload.m_ResourcePublicationQueue;
		const auto& after = statistics.m_ResourcePublicationQueue;
		const uint64_t enqueuedDelta = after.m_EnqueuedCount - before.m_EnqueuedCount;
		const uint64_t terminalDelta = (after.m_CompletedCount - before.m_CompletedCount) +
			(after.m_FailedCount - before.m_FailedCount) +
			(after.m_CancelledCount - before.m_CancelledCount);
		const auto require = [this](bool condition, std::string error)
			{
				if (!condition)
				{
					m_Suite->m_FinalErrors.push_back(std::move(error));
				}
			};
		require(enqueuedDelta == terminalDelta + after.m_PendingCount,
			"Publication queue conservation failed: enqueued != terminal + pending.");
		require(after.m_QueueSampleCount - before.m_QueueSampleCount == enqueuedDelta,
			"A yielded publication job resampled or lost its first-queued timestamp.");
		require(after.m_NoProgressContinueCount == before.m_NoProgressContinueCount,
			"The acceptance suite observed a no-progress Continue.");
		require(IsStreamingIdle(statistics),
			"Streaming queues, payload bytes, or in-flight staging are nonzero after the suite.");
		require(ownership.m_PublicationRetainCount ==
			m_Suite->m_BaselineOwnership.m_PublicationRetainCount,
			"Publication retains did not return to the suite baseline.");
		require(ownership.m_LeaseCount == m_Suite->m_BaselineOwnership.m_LeaseCount,
			"Asset leases did not return to the suite baseline.");
		require(ownership.m_OwnerCount == m_Suite->m_BaselineOwnership.m_OwnerCount,
			"Temporary asset owners did not return to the suite baseline.");
		require(statistics.m_CompletionCallbackFailureCount ==
			m_Suite->m_BaselineUpload.m_CompletionCallbackFailureCount,
			"An upload completion callback failed during the suite.");
		const uint64_t submittedDelta =
			statistics.m_SubmittedCount - m_Suite->m_BaselineUpload.m_SubmittedCount;
		const uint64_t finalizedDelta =
			(statistics.m_SucceededCount - m_Suite->m_BaselineUpload.m_SucceededCount) +
			(statistics.m_FailedCount - m_Suite->m_BaselineUpload.m_FailedCount);
		require(submittedDelta == finalizedDelta,
			"Upload submission conservation failed after GPU finalization.");

		m_Suite->m_Passed =
			m_Suite->m_FinalErrors.empty() &&
			std::ranges::all_of(m_Suite->m_Results, &AcceptanceSuiteState::Result::m_Passed);
		m_Suite->m_Finished = true;
		m_Suite->m_Phase = AcceptanceSuiteState::Phase::Completed;
		if (m_Suite->m_Passed)
		{
			GGLAB_LOG_INFO("ASSET PUBLICATION ACCEPTANCE PASS: {}/{} cases completed in {:.2f} s.",
				m_Suite->m_Results.size(), static_cast<size_t>(AcceptanceSuiteState::Case::Count),
				m_Suite->m_TotalElapsedSeconds);
		}
		else
		{
			GGLAB_LOG_ERROR(
				"ASSET PUBLICATION ACCEPTANCE FAIL: {}/{} cases passed, {} suite invariant errors.",
				std::ranges::count_if(m_Suite->m_Results, &AcceptanceSuiteState::Result::m_Passed),
				static_cast<size_t>(AcceptanceSuiteState::Case::Count),
				m_Suite->m_FinalErrors.size());
			for (const std::string& error : m_Suite->m_FinalErrors)
			{
				GGLAB_LOG_ERROR("Asset publication suite invariant: {}", error);
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
			.m_Description =
				"Deterministic one-step publication budgeting and stage fault injection for transaction rollback verification.",
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
