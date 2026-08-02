#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/NapaVoxelLabSession.h"

#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

namespace gglab
{
	namespace
	{
		constexpr uint64_t InitialOwnerGeneration = 1;

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeInitialConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 16,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 16, 16, 16 },
					},
			};
		}

		[[nodiscard]] std::array<napa::voxel::PrimitiveDesc, 2>
			MakeInitialPrimitives() noexcept
		{
			using namespace napa::voxel;
			return {
				PrimitiveDesc{
					.m_StableId = { 1 },
					.m_Material = VoxelMaterial::Soil,
					.m_Shape = PrimitiveShape::Sphere,
					.m_Parameters = {
						.m_Sphere = {
							.m_Center = { 1.25, 2.0, 2.0 },
							.m_Radius = 0.8,
							},
						},
					},
				PrimitiveDesc{
					.m_StableId = { 2 },
					.m_Material = VoxelMaterial::Stone,
					.m_Shape = PrimitiveShape::Sphere,
					.m_Parameters = {
						.m_Sphere = {
							.m_Center = { 2.75, 2.0, 2.0 },
							.m_Radius = 0.8,
							},
						},
					},
			};
		}

		[[nodiscard]] std::vector<napa::voxel::ChunkCoord> EnumerateCellOwnerChunks(
			const napa::voxel::VoxelWorldConfig& config) noexcept
		{
			napa::voxel::LogicalDomainMetrics metrics{};
			if (napa::voxel::ComputeLogicalDomainMetrics(config, metrics).Failed())
			{
				return {};
			}

			std::vector<napa::voxel::ChunkCoord> chunks;
			try
			{
				chunks.reserve(static_cast<size_t>(metrics.m_CellOwnerChunkCount));
				for (int32_t z = metrics.m_CellOwnerChunkBounds.m_Min.m_Z;
					z < metrics.m_CellOwnerChunkBounds.m_MaxExclusive.m_Z; ++z)
				{
					for (int32_t y = metrics.m_CellOwnerChunkBounds.m_Min.m_Y;
						y < metrics.m_CellOwnerChunkBounds.m_MaxExclusive.m_Y; ++y)
					{
						for (int32_t x = metrics.m_CellOwnerChunkBounds.m_Min.m_X;
							x < metrics.m_CellOwnerChunkBounds.m_MaxExclusive.m_X; ++x)
						{
							chunks.push_back({ x, y, z });
						}
					}
				}
			}
			catch (...)
			{
				return {};
			}
			return chunks;
		}
	}

	NapaVoxelLabSession::NapaVoxelLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		NapaVoxelLabSession(createInfo, std::make_shared<NapaVoxelRenderFrameSource>())
	{
	}

	NapaVoxelLabSession::NapaVoxelLabSession(const LabSessionCreateInfo& createInfo,
		std::shared_ptr<NapaVoxelRenderFrameSource> frameSource) noexcept :
		LabSessionBase(GetDescriptor(), createInfo,
			std::make_unique<RenderPipelineForwardPBR>(RenderPipelineForwardPBR::CreateInfo{
				.m_SceneExtension =
					std::make_unique<NapaVoxelRenderExtension>(frameSource),
				})),
		m_FrameSource(std::move(frameSource))
	{
		auto& profile = GetMutableViewRenderProfile();
		profile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;
		ApplyCameraPreset();
	}

	void NapaVoxelLabSession::BeginPrepare() noexcept
	{
		CancelPrepare();
		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.1f,
			.m_Stage = "Building static voxel publication",
			.m_Detail = "Generating and validating the initial CPU mesh set.",
		};
		if (!PrepareInitialPublication())
		{
			CancelPrepare();
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.0f,
				.m_Stage = "Static voxel preparation failed",
				.m_Detail = "Initial generation, validation, conversion, or resource creation failed.",
			};
			return;
		}

		ScheduleInitialUpload();
		UpdatePreparationState();
	}

	void NapaVoxelLabSession::TickPrepare() noexcept
	{
		UpdatePreparationState();
	}

	void NapaVoxelLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady() && m_RenderState.HasVisibleMeshes() &&
			m_RenderState.GetVisibleWorldRevision() == 1,
			"Napa voxel Lab committed before its CPU/GPU initial publication was ready.");
	}

	void NapaVoxelLabSession::CancelPrepare() noexcept
	{
		if (m_InitialPublication)
		{
			const AssetStreamingIdentity identity = m_InitialPublication->GetUploadIdentity();
			m_InitialPublication->Cancel();
			if (auto* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler())
			{
				GGLAB_UNUSED(scheduler->CancelReadyWork(identity));
			}
			m_InitialPublication.reset();
		}
		if (m_FrameSource)
		{
			m_FrameSource->ClearFrameView();
		}
		m_RenderState.Reset();
		m_VoxelWorld.reset();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void NapaVoxelLabSession::Update(float deltaTime) noexcept
	{
		UpdateCamera(deltaTime);
	}

	bool NapaVoxelLabSession::PrepareInitialPublication() noexcept
	{
		using namespace napa::voxel;

		const VoxelWorldConfig config = MakeInitialConfig();
		const auto primitives = MakeInitialPrimitives();
		PrimitiveWorldGenerationResult generation{};
		if (GeneratePrimitiveVoxelWorld(
			config, primitives, m_VoxelWorld, generation).Failed() || !m_VoxelWorld ||
			m_VoxelWorld->GetWorldVoxelRevision() != 1)
		{
			return false;
		}

		const std::vector<ChunkCoord> chunks = EnumerateCellOwnerChunks(config);
		if (chunks.empty())
		{
			return false;
		}
		CpuMeshBatch batch{};
		if (BuildCpuMeshBatch(
			*m_VoxelWorld, m_VoxelWorld->GetWorldVoxelRevision(), chunks, batch).Failed())
		{
			return false;
		}

		std::unique_ptr<PendingCpuMeshBatch> pendingCoreMeshes;
		if (ValidateCpuMeshBatch(
			batch, m_RenderState.GetVisibleCoreMeshes(), pendingCoreMeshes).Failed() ||
			!pendingCoreMeshes)
		{
			return false;
		}

		NapaVoxelCpuMeshSet cpuMeshes{};
		if (ConvertNapaVoxelMeshRecords(
			pendingCoreMeshes->GetChunks(), config, cpuMeshes).Failed())
		{
			return false;
		}

		uint64_t stableId = 0;
		if (!AllocateNapaVoxelPublicationStableId(stableId) ||
			!NapaVoxelInitialPublicationOwner::Create(config, std::move(pendingCoreMeshes),
				std::move(cpuMeshes), stableId, InitialOwnerGeneration, m_InitialPublication) ||
			!m_InitialPublication->PrepareGpuResources(m_Services.m_Renderer->GetDevice()) ||
			!m_InitialPublication->MarkQueued())
		{
			return false;
		}
		return true;
	}

	void NapaVoxelLabSession::ScheduleInitialUpload() noexcept
	{
		auto* scheduler = m_Services.m_Renderer->GetAssetUploadScheduler();
		GGLAB_ASSERT_NOT_NULL(scheduler);
		if (!scheduler || !m_InitialPublication)
		{
			if (m_InitialPublication)
			{
				m_InitialPublication->Fail();
			}
			return;
		}

		const std::shared_ptr<NapaVoxelInitialPublicationOwner> publication =
			m_InitialPublication;
		const AssetStreamingIdentity identity = publication->GetUploadIdentity();
		const AssetStreamingWorkEstimate estimate = publication->GetUploadEstimate();
		scheduler->EnqueueUploadRecording({
			.m_Name = "Napa Voxel Initial Mesh Upload",
			.m_Identity = identity,
			.m_Estimate = estimate,
			.m_Priority = TaskPriority::High,
			},
			[scheduler, publication, identity, estimate]() noexcept
			{
				if (!publication->BeginRecording(identity))
				{
					return;
				}
				const AssetUploadHandle handle = scheduler->RecordUpload({
					.m_Name = "Napa Voxel Initial Mesh Publication",
					.m_Identity = identity,
					.m_Estimate = estimate,
					.m_Priority = TaskPriority::High,
					},
					[publication](TransferBatch& batch) noexcept
					{ return publication->RecordUpload(batch); },
					[publication](const AssetUploadCompletionInfo& completion) noexcept
					{ GGLAB_UNUSED(publication->CompleteUpload(completion)); });
				GGLAB_UNUSED(publication->SetUploadHandle(handle));
			});
	}

	void NapaVoxelLabSession::UpdatePreparationState() noexcept
	{
		if (!m_InitialPublication)
		{
			return;
		}

		switch (m_InitialPublication->GetStatus())
		{
		case NapaVoxelInitialPublicationStatus::Prepared:
		case NapaVoxelInitialPublicationStatus::Queued:
		case NapaVoxelInitialPublicationStatus::Recording:
		case NapaVoxelInitialPublicationStatus::AwaitingFence:
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Preparing,
				.m_Fraction = 0.75f,
				.m_Stage = "Uploading static voxel meshes",
				.m_Detail = "Waiting for the publication Copy Fence.",
			};
			break;
		case NapaVoxelInitialPublicationStatus::ReadyForCommit:
			if (m_RenderState.PrepareInitialCommit(m_InitialPublication))
			{
				const uint64_t targetWorldRevision =
					m_InitialPublication->GetTargetWorldRevision();
				m_RenderState.CommitInitial(m_InitialPublication);
				if (m_RenderState.HasVisibleMeshes() &&
					m_RenderState.GetVisibleWorldRevision() == targetWorldRevision)
				{
					m_FrameSource->SetFrameView(m_RenderState.GetVisibleGpuMeshes());
					m_LoadingProgress = LoadingProgress::Ready();
				}
				else
				{
					m_LoadingProgress = {
						.m_Status = LoadingStatus::Failed,
						.m_Fraction = 0.9f,
						.m_Stage = "Static voxel publication invariant failed",
						.m_Detail = "A prepared CPU/GPU commit did not publish one revision.",
					};
				}
			}
			else
			{
				m_LoadingProgress = {
					.m_Status = LoadingStatus::Failed,
					.m_Fraction = 0.9f,
					.m_Stage = "Static voxel publication failed",
					.m_Detail = "Fence completion could not be committed atomically.",
				};
			}
			break;
		case NapaVoxelInitialPublicationStatus::Committed:
			m_LoadingProgress = LoadingProgress::Ready();
			break;
		case NapaVoxelInitialPublicationStatus::Failed:
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.75f,
				.m_Stage = "Static voxel upload failed",
				.m_Detail = "GPU resources or upload recording did not complete successfully.",
			};
			break;
		case NapaVoxelInitialPublicationStatus::Cancelled:
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.0f,
				.m_Stage = "Static voxel preparation cancelled",
				.m_Detail = "The publication owner generation was invalidated.",
			};
			break;
		case NapaVoxelInitialPublicationStatus::Uninitialized:
			break;
		}
	}

	void NapaVoxelLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(2.0f, 2.4f, -5.0f), Vector3(2.0f, 2.0f, 2.0f));
		GetCamera().SetNearFar(0.05f, 100.0f);
		GetCamera().Update();
	}

	LabId NapaVoxelLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.napa_voxel_p0");
	}

	LabDescriptor NapaVoxelLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Napa Voxel P0",
			.m_Category = "Napa / Voxel",
			.m_Description =
				"Validates static Napa voxel mesh publication, upload, and opaque rendering.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> NapaVoxelLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<NapaVoxelLabSession>(createInfo);
	}
}
