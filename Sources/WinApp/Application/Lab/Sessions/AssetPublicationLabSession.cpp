#include "Application/Lab/Sessions/AssetPublicationLabSession.h"
#include "AppRuntimeLog.h"
#include "GGLabRuntime/Diagnostics/AssetSnapshotRead.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadControl.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"

#include <algorithm>
#include <format>
#include <utility>

namespace gglab
{
	namespace
	{
		constexpr const char* IncrementalModelPath = "Assets/Models/Sponza/Sponza.gltf";
		constexpr const char* HeldTexturePath =
			"Assets/Models/FlightHelmet/FlightHelmet_Materials_LensesMat_BaseColor.png";
		constexpr float CaseTimeoutSeconds = 120.0f;

		[[nodiscard]] bool HasPendingPublication(const AssetUploadStatistics& statistics,
			const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(statistics.m_ResourcePublicationQueue.m_PendingWork,
				[&identity](const AssetStreamingWorkActivity& work) noexcept
				{ return work.m_Identity == identity; });
		}

		[[nodiscard]] bool HasPendingUpload(const AssetUploadStatistics& statistics,
			const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(statistics.m_PendingUploads,
				[&identity](const AssetUploadActivity& upload) noexcept
				{ return upload.m_Identity == identity; });
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
	}

	struct AssetPublicationLabSession::State
	{
		enum class Phase : uint8_t
		{
			IncrementalPublication,
			WaitingGpuSubmission,
			WaitingGpuCancellation,
			Completed,
		};

		struct Result
		{
			std::string m_Name;
			std::vector<std::string> m_Errors;
		};

		Phase m_Phase = Phase::IncrementalPublication;
		AssetOwnerScope m_Owner;
		AssetUploadStatistics m_BaselineUpload{};
		AssetUploadStatistics m_CaseBaselineUpload{};
		uint64_t m_BaselineGpuDeferredCancellations = 0;
		AssetManager::ModelLoadRequest m_ModelRequest{};
		AssetManager::TextureLoadRequest m_TextureRequest{};
		AssetStreamingIdentity m_Identity{};
		uint64_t m_LastProcessed = 0;
		uint32_t m_FramesWithPublicationSteps = 0;
		uint32_t m_SettleFrames = 0;
		float m_CaseElapsedSeconds = 0.0f;
		float m_TotalElapsedSeconds = 0.0f;
		bool m_SawQueuedJob = false;
		bool m_HalfPublishedModelObserved = false;
		bool m_GpuHoldObserved = false;
		bool m_Passed = false;
		std::vector<Result> m_Results;
		std::vector<std::string> m_SuiteErrors;
	};

	AssetPublicationLabSession::AssetPublicationLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPlus())
	{
	}

	AssetPublicationLabSession::~AssetPublicationLabSession() = default;

	void AssetPublicationLabSession::OnEnter() noexcept
	{
		AssetUploadControl* control = m_Services.m_AssetUploadControl;
		if (control == nullptr)
		{
			GGLAB_LOG_ERROR("Asset publication Lab requires the asset upload control capability.");
			return;
		}
		m_OriginalBudget = control->GetFrameBudget();
		m_HasOriginalBudget = true;
		// One publication step per frame spreads model publication across frames.
		AssetStreamingFrameBudget stressBudget = m_OriginalBudget;
		stressBudget.m_MaxResourcePublicationSteps = 1;
		stressBudget.m_MaxResourcePublicationCreations = 1;
		stressBudget.m_MaxResourcePublicationMilliseconds = 0.05;
		control->SetFrameBudget(stressBudget);

		m_State = std::make_unique<State>();
		m_State->m_Owner = m_Services.m_AssetManager->CreateOwnerScope();
		m_State->m_BaselineUpload = m_Services.m_RenderServices.m_AssetUpload->GetStatistics();
		m_State->m_BaselineGpuDeferredCancellations =
			m_Services.m_AssetManager->GetOwnershipStatistics().m_GpuDeferredCancellationCount;
		StartIncrementalPublication();
	}

	void AssetPublicationLabSession::OnExit() noexcept
	{
		Stop();
	}

	void AssetPublicationLabSession::Stop() noexcept
	{
		if (AssetUploadControl* control = m_Services.m_AssetUploadControl)
		{
			control->ClearGpuCompletionHold();
			if (m_HasOriginalBudget)
			{
				control->SetFrameBudget(m_OriginalBudget);
			}
		}
		m_State.reset();
	}

	void AssetPublicationLabSession::Update(float deltaTime) noexcept
	{
		GetCamera().Update();
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}
		m_State->m_TotalElapsedSeconds += deltaTime;
		m_State->m_CaseElapsedSeconds += deltaTime;
		if (m_State->m_CaseElapsedSeconds > CaseTimeoutSeconds)
		{
			if (AssetUploadControl* control = m_Services.m_AssetUploadControl)
			{
				control->ClearGpuCompletionHold();
			}
			CompleteCase("Timed out acceptance case", { "The acceptance case exceeded its timeout." });
			CompleteSuite();
			return;
		}
		switch (m_State->m_Phase)
		{
		case State::Phase::IncrementalPublication:
			UpdateIncrementalPublication();
			break;
		case State::Phase::WaitingGpuSubmission:
		case State::Phase::WaitingGpuCancellation:
			UpdateGpuCancellation();
			break;
		case State::Phase::Completed:
			break;
		}
	}

	void AssetPublicationLabSession::StartIncrementalPublication() noexcept
	{
		m_State->m_CaseBaselineUpload = m_Services.m_RenderServices.m_AssetUpload->GetStatistics();
		m_State->m_LastProcessed =
			m_State->m_CaseBaselineUpload.m_ResourcePublicationQueue.m_ProcessedCount;
		m_State->m_ModelRequest =
			m_State->m_Owner.LoadModelAsync(IncrementalModelPath, TaskPriority::Normal);
		if (!m_State->m_ModelRequest.IsValid())
		{
			CompleteCase("Incremental publication", { "AssetManager rejected the model load request." });
			StartGpuCancellation();
			return;
		}
		m_State->m_Identity = {
			.m_Kind = AssetStreamingWorkKind::Model,
			.m_StableId = m_State->m_ModelRequest.m_ModelId.Value(),
			.m_Generation = m_State->m_ModelRequest.m_Generation,
		};
	}

	void AssetPublicationLabSession::UpdateIncrementalPublication() noexcept
	{
		const AssetUploadStatistics statistics =
			m_Services.m_RenderServices.m_AssetUpload->GetStatistics();
		const auto& publication = statistics.m_ResourcePublicationQueue;
		if (publication.m_ProcessedCount > m_State->m_LastProcessed)
		{
			++m_State->m_FramesWithPublicationSteps;
			m_State->m_LastProcessed = publication.m_ProcessedCount;
		}
		m_State->m_SawQueuedJob |= HasPendingPublication(statistics, m_State->m_Identity);
		const Model* model = m_Services.m_AssetManager->GetModel(m_State->m_ModelRequest.m_ModelId);
		if (!model || model->m_ContentGeneration != m_State->m_ModelRequest.m_Generation)
		{
			return;
		}
		m_State->m_HalfPublishedModelObserved |=
			model->m_State == AssetState::Publishing && !model->m_MeshInstance.empty();
		const bool terminal = model->m_State == AssetState::Ready ||
			model->m_State == AssetState::Failed || model->m_State == AssetState::Cancelled;
		if (!terminal || !IsStreamingIdle(statistics) || ++m_State->m_SettleFrames < 8)
		{
			return;
		}

		const auto& before = m_State->m_CaseBaselineUpload.m_ResourcePublicationQueue;
		std::vector<std::string> errors;
		const auto require = [&errors](bool condition, const char* error)
			{
				if (!condition)
				{
					errors.emplace_back(error);
				}
			};
		require(model->m_State == AssetState::Ready, "The model did not publish to Ready.");
		require(publication.m_CompletedCount == before.m_CompletedCount + 1,
			"The model did not complete exactly one publication job.");
		require(m_State->m_SawQueuedJob && publication.m_ContinueCount > before.m_ContinueCount + 1 &&
			m_State->m_FramesWithPublicationSteps > 1,
			"The one-step budget did not spread publication across multiple frames.");
		require(!m_State->m_HalfPublishedModelObserved,
			"The model exposed mesh instances while still Publishing.");
		CompleteCase("Incremental publication", std::move(errors));
		StartGpuCancellation();
	}

	void AssetPublicationLabSession::StartGpuCancellation() noexcept
	{
		m_State->m_CaseElapsedSeconds = 0.0f;
		m_State->m_SettleFrames = 0;
		m_State->m_Owner.Reset();
		m_State->m_TextureRequest = m_State->m_Owner.LoadTextureAsync(
			HeldTexturePath, TextureSemantic::BaseColor, TaskPriority::Normal);
		if (!m_State->m_TextureRequest.IsValid())
		{
			CompleteCase("GPU submitted cancellation", { "AssetManager rejected the texture request." });
			CompleteSuite();
			return;
		}
		m_State->m_Identity = {
			.m_Kind = AssetStreamingWorkKind::Texture,
			.m_StableId = m_State->m_TextureRequest.m_TextureId.Value(),
			.m_Generation = m_State->m_TextureRequest.m_Generation,
		};
		m_Services.m_AssetUploadControl->ArmGpuCompletionHold(m_State->m_Identity);
		m_State->m_Phase = State::Phase::WaitingGpuSubmission;
	}

	void AssetPublicationLabSession::UpdateGpuCancellation() noexcept
	{
		AssetManager& assets = *m_Services.m_AssetManager;
		AssetUploadControl& control = *m_Services.m_AssetUploadControl;
		const AssetUploadStatistics statistics =
			m_Services.m_RenderServices.m_AssetUpload->GetStatistics();
		const AssetSnapshot snapshot = BuildAssetSnapshot(assets);
		const AssetSnapshot::Texture* texture =
			FindTextureSnapshot(snapshot, m_State->m_TextureRequest.m_TextureId);
		if (m_State->m_Phase == State::Phase::WaitingGpuSubmission)
		{
			if (!HasPendingUpload(statistics, m_State->m_Identity))
			{
				return;
			}
			std::vector<std::string> errors;
			m_State->m_GpuHoldObserved = texture && texture->m_State == AssetState::GpuProcessing &&
				texture->m_Texture.IsValid();
			if (!m_State->m_GpuHoldObserved)
			{
				errors.emplace_back("The held upload was not an allocated GPU resource.");
			}
			m_State->m_Owner.Reset();
			const AssetSnapshot cancelled = BuildAssetSnapshot(assets);
			const AssetSnapshot::Texture* cancelledTexture =
				FindTextureSnapshot(cancelled, m_State->m_TextureRequest.m_TextureId);
			if (!cancelledTexture || cancelledTexture->m_State != AssetState::GpuProcessing ||
				!cancelledTexture->m_Texture.IsValid())
			{
				errors.emplace_back("Post-submit cancellation destroyed the texture before its fence.");
			}
			control.ClearGpuCompletionHold();
			if (!errors.empty())
			{
				CompleteCase("GPU submitted cancellation", std::move(errors));
				CompleteSuite();
				return;
			}
			m_State->m_Phase = State::Phase::WaitingGpuCancellation;
			return;
		}

		if (!texture || texture->m_State != AssetState::Cancelled ||
			HasPendingUpload(statistics, m_State->m_Identity) || !IsStreamingIdle(statistics) ||
			++m_State->m_SettleFrames < 4)
		{
			return;
		}
		std::vector<std::string> errors;
		if (texture->m_Texture.IsValid() || texture->m_IsUploaded)
		{
			errors.emplace_back("Cancelled GPU resources survived fence-safe finalization.");
		}
		if (assets.GetOwnershipStatistics().m_GpuDeferredCancellationCount !=
			m_State->m_BaselineGpuDeferredCancellations + 1)
		{
			errors.emplace_back("GPU deferred cancellation telemetry did not advance exactly once.");
		}
		CompleteCase("GPU submitted cancellation", std::move(errors));
		CompleteSuite();
	}

	void AssetPublicationLabSession::CompleteCase(
		std::string name, std::vector<std::string> errors) noexcept
	{
		if (errors.empty())
		{
			GGLAB_LOG_INFO("Asset publication acceptance case PASS: {}.", name);
		}
		else
		{
			GGLAB_LOG_ERROR("Asset publication acceptance case FAIL: {} ({} errors).",
				name, errors.size());
			for (const std::string& error : errors)
			{
				GGLAB_LOG_ERROR("Asset publication acceptance invariant: {}", error);
			}
		}
		m_State->m_Results.push_back({ .m_Name = std::move(name), .m_Errors = std::move(errors) });
	}

	void AssetPublicationLabSession::CompleteSuite() noexcept
	{
		// Upload conservation on the device; ownership conservation is exact only
		// in the headless suite because other sessions share this AssetManager.
		const AssetUploadStatistics statistics =
			m_Services.m_RenderServices.m_AssetUpload->GetStatistics();
		const AssetUploadStatistics& baseline = m_State->m_BaselineUpload;
		const uint64_t submitted = statistics.m_SubmittedCount - baseline.m_SubmittedCount;
		const uint64_t finalized = (statistics.m_SucceededCount - baseline.m_SucceededCount) +
			(statistics.m_FailedCount - baseline.m_FailedCount);
		if (submitted != finalized)
		{
			m_State->m_SuiteErrors.emplace_back(
				"Upload submission conservation failed after GPU finalization.");
		}
		if (statistics.m_CompletionCallbackFailureCount != baseline.m_CompletionCallbackFailureCount)
		{
			m_State->m_SuiteErrors.emplace_back("An upload completion callback failed.");
		}
		if (!IsStreamingIdle(statistics))
		{
			m_State->m_SuiteErrors.emplace_back("Streaming queues or in-flight staging did not settle.");
		}
		const size_t passed = std::ranges::count_if(m_State->m_Results,
			[](const State::Result& result) noexcept { return result.m_Errors.empty(); });
		m_State->m_Passed = m_State->m_SuiteErrors.empty() && passed == m_State->m_Results.size();
		m_State->m_Phase = State::Phase::Completed;
		m_State->m_Owner = {};
		if (m_State->m_Passed)
		{
			GGLAB_LOG_INFO("ASSET PUBLICATION ACCEPTANCE PASS: {}/{} GPU cases completed in {:.2f} s.",
				passed, m_State->m_Results.size(), m_State->m_TotalElapsedSeconds);
		}
		else
		{
			GGLAB_LOG_ERROR("ASSET PUBLICATION ACCEPTANCE FAIL: {}/{} GPU cases passed, {} suite errors.",
				passed, m_State->m_Results.size(), m_State->m_SuiteErrors.size());
			for (const std::string& error : m_State->m_SuiteErrors)
			{
				GGLAB_LOG_ERROR("Asset publication suite invariant: {}", error);
			}
		}
	}

	void AssetPublicationLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Asset Publication GPU Acceptance";
		if (!m_State)
		{
			return;
		}
		const bool completed = m_State->m_Phase == State::Phase::Completed;
		diagnostics.m_Metrics = {
			{.m_Name = "Completed cases", .m_Value = std::to_string(m_State->m_Results.size())},
			{.m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_TotalElapsedSeconds)},
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "GPU acceptance",
			.m_Status = !completed ? LabDiagnosticCheckStatus::Pending
				: m_State->m_Passed ? LabDiagnosticCheckStatus::Passed
				: LabDiagnosticCheckStatus::Failed,
			.m_Detail = !completed ? "GPU acceptance cases are running."
				: m_State->m_Passed ? "All GPU publication cases passed."
				: "One or more GPU publication cases failed.",
			});
		for (const State::Result& result : m_State->m_Results)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = result.m_Name,
				.m_Status = result.m_Errors.empty() ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Failed,
				.m_Detail = result.m_Errors.empty() ? "Passed." : result.m_Errors.front(),
				});
		}
		for (const std::string& error : m_State->m_SuiteErrors)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Suite invariant",
				.m_Status = LabDiagnosticCheckStatus::Failed,
				.m_Detail = error,
				});
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
				"GPU acceptance for incremental publication, fence-finalized cancellation and upload conservation.",
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
