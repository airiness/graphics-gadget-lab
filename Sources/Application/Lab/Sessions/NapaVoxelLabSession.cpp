#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/NapaVoxelLabSession.h"

#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <chrono>

namespace gglab
{
	namespace
	{
		constexpr uint64_t InitialOwnerGeneration = 1;

		const LabParameterId PresetId("napa_voxel.scenario.preset");
		const LabParameterId ChunkCellCountId("napa_voxel.scenario.chunk_cell_count");
		const LabParameterId VoxelSizeId("napa_voxel.scenario.voxel_size");
		const LabParameterId SurfaceBandId("napa_voxel.scenario.surface_band");
		const LabParameterId ShowChunkBoundsId("napa_voxel.debug.show_chunk_bounds");
		const LabParameterId SurfaceModeId("napa_voxel.debug.surface_mode");
		const StringID ChunkBoundsChannel("NapaVoxel.ChunkBounds");

		enum class StaticPreset : int32_t
		{
			SingleChunk,
			BoundaryFace,
			BoundaryEdge,
			BoundaryCorner,
			NegativeChunk,
			EmptyPrimitiveSet,
		};

		enum class ChunkCellCountOption : int32_t
		{
			Cells8 = 8,
			Cells16 = 16,
			Cells32 = 32,
		};

		[[nodiscard]] const char* GetPresetName(StaticPreset preset) noexcept
		{
			switch (preset)
			{
			case StaticPreset::SingleChunk:
				return "Single Chunk";
			case StaticPreset::BoundaryFace:
				return "Boundary Face";
			case StaticPreset::BoundaryEdge:
				return "Boundary Edge";
			case StaticPreset::BoundaryCorner:
				return "Boundary Corner";
			case StaticPreset::NegativeChunk:
				return "Negative Chunk";
			case StaticPreset::EmptyPrimitiveSet:
				return "Empty Primitive Set";
			}
			return "Unknown";
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeConfig(StaticPreset preset,
			uint32_t chunkCellCount, float voxelSize, float surfaceBand) noexcept
		{
			using namespace napa::voxel;
			const int32_t count = static_cast<int32_t>(chunkCellCount);
			CellAabb bounds{
				.m_Min = {},
				.m_MaxExclusive = { count, count, count },
			};
			switch (preset)
			{
			case StaticPreset::BoundaryFace:
				bounds.m_MaxExclusive.m_X = count * 2;
				break;
			case StaticPreset::BoundaryEdge:
				bounds.m_MaxExclusive = { count * 2, count * 2, count };
				break;
			case StaticPreset::BoundaryCorner:
				bounds.m_MaxExclusive = { count * 2, count * 2, count * 2 };
				break;
			case StaticPreset::NegativeChunk:
				bounds = {
					.m_Min = { -count * 2, -count, count },
					.m_MaxExclusive = { 0, count, count * 2 },
				};
				break;
			case StaticPreset::SingleChunk:
			case StaticPreset::EmptyPrimitiveSet:
				break;
			}

			return {
				.m_ChunkCellCount = chunkCellCount,
				.m_VoxelSize = voxelSize,
				.m_SurfaceBandVoxels = surfaceBand,
				.m_LogicalCellBounds = bounds,
			};
		}

		[[nodiscard]] napa::voxel::Double3 ToWorldPosition(
			napa::voxel::Double3 cellPosition, double voxelSize) noexcept
		{
			return {
				cellPosition.m_X * voxelSize,
				cellPosition.m_Y * voxelSize,
				cellPosition.m_Z * voxelSize,
			};
		}

		void AddSphere(std::vector<napa::voxel::PrimitiveDesc>& primitives, uint64_t stableId,
			napa::voxel::VoxelMaterial material, napa::voxel::Double3 centerInCells,
			double radiusInCells, double voxelSize)
		{
			using namespace napa::voxel;
			primitives.push_back({
				.m_StableId = { stableId },
				.m_Material = material,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = ToWorldPosition(centerInCells, voxelSize),
						.m_Radius = radiusInCells * voxelSize,
					},
				},
			});
		}

		[[nodiscard]] std::vector<napa::voxel::PrimitiveDesc> MakePrimitives(
			StaticPreset preset, const napa::voxel::VoxelWorldConfig& config)
		{
			using namespace napa::voxel;
			std::vector<PrimitiveDesc> primitives;
			if (preset == StaticPreset::EmptyPrimitiveSet)
			{
				return primitives;
			}

			const double count = static_cast<double>(config.m_ChunkCellCount);
			const double voxelSize = static_cast<double>(config.m_VoxelSize);
			if (preset == StaticPreset::SingleChunk)
			{
				primitives.reserve(2);
				AddSphere(primitives, 1, VoxelMaterial::Soil,
					{ count * 0.28, count * 0.5, count * 0.5 }, count * 0.14, voxelSize);
				AddSphere(primitives, 2, VoxelMaterial::Stone,
					{ count * 0.72, count * 0.5, count * 0.5 }, count * 0.14, voxelSize);
				return primitives;
			}

			Double3 center{};
			switch (preset)
			{
			case StaticPreset::BoundaryFace:
				center = { count, count * 0.5, count * 0.5 };
				break;
			case StaticPreset::BoundaryEdge:
				center = { count, count, count * 0.5 };
				break;
			case StaticPreset::BoundaryCorner:
				center = { count, count, count };
				break;
			case StaticPreset::NegativeChunk:
				center = { -count, 0.0, count * 1.5 };
				break;
			case StaticPreset::SingleChunk:
			case StaticPreset::EmptyPrimitiveSet:
				break;
			}
			primitives.reserve(1);
			AddSphere(primitives, 1, VoxelMaterial::Stone, center, count * 0.24, voxelSize);
			return primitives;
		}

		[[nodiscard]] std::vector<napa::voxel::ChunkCoord> EnumerateCellOwnerChunks(
			const napa::voxel::VoxelWorldConfig& config)
		{
			napa::voxel::LogicalDomainMetrics metrics{};
			if (napa::voxel::ComputeLogicalDomainMetrics(config, metrics).Failed())
			{
				return {};
			}

			std::vector<napa::voxel::ChunkCoord> chunks;
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
			return chunks;
		}

		template<typename Rep, typename Period>
		[[nodiscard]] double Milliseconds(std::chrono::duration<Rep, Period> duration) noexcept
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}

		[[nodiscard]] const char* GetPublicationStatusName(
			NapaVoxelInitialPublicationStatus status) noexcept
		{
			switch (status)
			{
			case NapaVoxelInitialPublicationStatus::Uninitialized:
				return "Uninitialized";
			case NapaVoxelInitialPublicationStatus::Prepared:
				return "Prepared";
			case NapaVoxelInitialPublicationStatus::Queued:
				return "Queued";
			case NapaVoxelInitialPublicationStatus::Recording:
				return "Recording";
			case NapaVoxelInitialPublicationStatus::AwaitingFence:
				return "Awaiting Copy Fence";
			case NapaVoxelInitialPublicationStatus::ReadyForCommit:
				return "Ready for Commit";
			case NapaVoxelInitialPublicationStatus::Committed:
				return "Committed";
			case NapaVoxelInitialPublicationStatus::Failed:
				return "Failed";
			case NapaVoxelInitialPublicationStatus::Cancelled:
				return "Cancelled";
			}
			return "Unknown";
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
				.m_SceneExtension = std::make_unique<NapaVoxelRenderExtension>(frameSource),
				})),
		m_FrameSource(std::move(frameSource))
	{
		auto* renderer = m_Services.m_Renderer;
		m_PublicationSession = std::make_unique<NapaVoxelStaticPublicationSession>(
			renderer ? renderer->GetDevice() : nullptr,
			renderer ? renderer->GetAssetUploadScheduler() : nullptr);

		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = PresetId,
			.m_Name = "Preset",
			.m_Group = "Scenario",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = static_cast<int32_t>(StaticPreset::SingleChunk),
			.m_EnumItems = {
				{ static_cast<int32_t>(StaticPreset::SingleChunk), "Single Chunk" },
				{ static_cast<int32_t>(StaticPreset::BoundaryFace), "Boundary Face" },
				{ static_cast<int32_t>(StaticPreset::BoundaryEdge), "Boundary Edge" },
				{ static_cast<int32_t>(StaticPreset::BoundaryCorner), "Boundary Corner" },
				{ static_cast<int32_t>(StaticPreset::NegativeChunk), "Negative Chunk" },
				{ static_cast<int32_t>(StaticPreset::EmptyPrimitiveSet), "Empty Primitive Set" },
			},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ChunkCellCountId,
			.m_Name = "Chunk Cell Count",
			.m_Group = "Scenario",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = static_cast<int32_t>(ChunkCellCountOption::Cells16),
			.m_EnumItems = {
				{ static_cast<int32_t>(ChunkCellCountOption::Cells8), "8" },
				{ static_cast<int32_t>(ChunkCellCountOption::Cells16), "16" },
				{ static_cast<int32_t>(ChunkCellCountOption::Cells32), "32" },
			},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = VoxelSizeId,
			.m_Name = "Voxel Size",
			.m_Group = "Scenario",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = 0.25f,
			.m_MinValue = 0.05f,
			.m_MaxValue = 2.0f,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = SurfaceBandId,
			.m_Name = "Surface Band",
			.m_Group = "Scenario",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = 2.0f,
			.m_MinValue = 1.0f,
			.m_MaxValue = 4.0f,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ShowChunkBoundsId,
			.m_Name = "Show Chunk Bounds",
			.m_Group = "Debug",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = SurfaceModeId,
			.m_Name = "Surface Mode",
			.m_Group = "Debug",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t{ 0 },
			.m_EnumItems = { { 0, "Shaded" } },
			}));

		auto& profile = GetMutableViewRenderProfile();
		profile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;
		ApplyImmediateParameters();
	}

	void NapaVoxelLabSession::BeginPrepare() noexcept
	{
		CancelPrepare();
		m_InitialVoxelHash = 0;
		m_LastGenerationMilliseconds = 0.0;
		m_LastMeshingMilliseconds = 0.0;
		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.1f,
			.m_Stage = "Building static voxel publication",
			.m_Detail = "Generating and validating the selected CPU mesh set.",
		};
		if (!PrepareInitialPublication())
		{
			CancelPrepare();
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.0f,
				.m_Stage = "Static voxel preparation failed",
				.m_Detail = "Generation, validation, conversion, or resource creation failed.",
			};
			return;
		}
		UpdatePreparationState();
	}

	void NapaVoxelLabSession::TickPrepare() noexcept
	{
		if (m_PublicationSession)
		{
			m_PublicationSession->TickPrepare();
		}
		UpdatePreparationState();
	}

	void NapaVoxelLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady() && m_PublicationSession &&
			m_PublicationSession->HasVisibleMeshes() &&
			m_PublicationSession->GetVisibleWorldRevision() == 1,
			"Napa voxel Lab committed before its CPU/GPU publication was ready.");
	}

	void NapaVoxelLabSession::CancelPrepare() noexcept
	{
		if (m_PublicationSession)
		{
			m_PublicationSession->CancelPrepare();
		}
		if (m_FrameSource)
		{
			m_FrameSource->ClearFrameView();
		}
		m_VoxelWorld.reset();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void NapaVoxelLabSession::OnEnter() noexcept
	{
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->SetChannelEnabled(ChunkBoundsChannel, m_ShowChunkBounds);
		}
	}

	void NapaVoxelLabSession::OnExit() noexcept
	{
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->ClearChannel(ChunkBoundsChannel);
		}
	}

	void NapaVoxelLabSession::Update(float deltaTime) noexcept
	{
		UpdateCamera(deltaTime);
		DrawChunkBounds();
	}

	bool NapaVoxelLabSession::PrepareInitialPublication() noexcept
	{
		try
		{
			return PrepareInitialPublicationInternal();
		}
		catch (...)
		{
			return false;
		}
	}

	bool NapaVoxelLabSession::PrepareInitialPublicationInternal()
	{
		using namespace napa::voxel;

		const auto& parameters = GetParameters();
		const StaticPreset preset = static_cast<StaticPreset>(parameters.Get(
			PresetId, static_cast<int32_t>(StaticPreset::SingleChunk)));
		const int32_t chunkCellCount = parameters.Get(ChunkCellCountId,
			static_cast<int32_t>(ChunkCellCountOption::Cells16));
		if (!IsSupportedChunkCellCount(static_cast<uint32_t>(chunkCellCount)))
		{
			return false;
		}
		m_PresetName = GetPresetName(preset);
		m_CurrentConfig = MakeConfig(preset,
			static_cast<uint32_t>(chunkCellCount),
			parameters.Get(VoxelSizeId, 0.25f), parameters.Get(SurfaceBandId, 2.0f));
		if (ValidateConfig(m_CurrentConfig).Failed())
		{
			return false;
		}
		ApplyCameraPreset();

		const std::vector<PrimitiveDesc> primitives = MakePrimitives(preset, m_CurrentConfig);
		PrimitiveWorldGenerationResult generation{};
		const auto generationBegin = std::chrono::steady_clock::now();
		const ValidationResult generationResult = GeneratePrimitiveVoxelWorld(
			m_CurrentConfig, primitives, m_VoxelWorld, generation);
		m_LastGenerationMilliseconds = Milliseconds(
			std::chrono::steady_clock::now() - generationBegin);
		if (generationResult.Failed() || !m_VoxelWorld ||
			m_VoxelWorld->GetWorldVoxelRevision() != 1)
		{
			return false;
		}
		m_InitialVoxelHash = generation.m_InitialVoxelHash;

		const std::vector<ChunkCoord> chunks = EnumerateCellOwnerChunks(m_CurrentConfig);
		if (chunks.empty())
		{
			return false;
		}
		const auto meshingBegin = std::chrono::steady_clock::now();
		CpuMeshBatch batch{};
		if (BuildCpuMeshBatch(
			*m_VoxelWorld, m_VoxelWorld->GetWorldVoxelRevision(), chunks, batch).Failed())
		{
			return false;
		}

		std::unique_ptr<PendingCpuMeshBatch> pendingCoreMeshes;
		if (ValidateCpuMeshBatch(batch, m_PublicationSession->GetVisibleCoreMeshes(),
			pendingCoreMeshes).Failed() || !pendingCoreMeshes)
		{
			return false;
		}
		m_LastMeshingMilliseconds = Milliseconds(
			std::chrono::steady_clock::now() - meshingBegin);

		uint64_t stableId = 0;
		return AllocateNapaVoxelPublicationStableId(stableId) &&
			m_PublicationSession->BeginPrepare(
				pendingCoreMeshes, stableId, InitialOwnerGeneration);
	}

	void NapaVoxelLabSession::UpdatePreparationState() noexcept
	{
		if (!m_PublicationSession)
		{
			return;
		}
		if (m_PublicationSession->IsReady())
		{
			m_FrameSource->SetFrameView(m_PublicationSession->GetFrameView());
			m_LoadingProgress = LoadingProgress::Ready();
			return;
		}
		if (m_PublicationSession->HasFailed())
		{
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.75f,
				.m_Stage = "Static voxel publication failed",
				.m_Detail = "The prepared CPU/GPU publication could not be completed.",
			};
			return;
		}

		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.75f,
			.m_Stage = "Uploading static voxel meshes",
			.m_Detail = std::format("Publication state: {}.", GetPublicationStatusName(
				m_PublicationSession->GetPublicationStatus())),
		};
	}

	void NapaVoxelLabSession::DrawChunkBounds() noexcept
	{
		if (!m_ShowChunkBounds || !m_PublicationSession ||
			!m_PublicationSession->HasVisibleMeshes() || !m_Services.m_DebugDraw)
		{
			return;
		}

		const auto& visible = m_PublicationSession->GetVisibleCoreMeshes();
		const auto& config = visible.GetConfig();
		const float chunkSize = static_cast<float>(config.m_ChunkCellCount) * config.m_VoxelSize;
		const Vector3 extents(chunkSize * 0.5f);
		const DebugDrawStyle style{
			.m_Color = Color::Cyan,
			.m_Channel = ChunkBoundsChannel,
		};
		for (const napa::voxel::ChunkMeshRecord& record : visible.GetChunks())
		{
			NapaVoxelWorldPosition origin{};
			Vector3 translation{};
			if (ComputeNapaVoxelChunkOrigin(config, record.m_Chunk, origin).Succeeded() &&
				ComputeNapaVoxelRenderTranslation(config, origin, {}, translation).Succeeded())
			{
				m_Services.m_DebugDraw->Box(translation + extents, extents, style);
			}
		}
	}

	void NapaVoxelLabSession::ApplyCameraPreset() noexcept
	{
		const auto& bounds = m_CurrentConfig.m_LogicalCellBounds;
		const float voxelSize = m_CurrentConfig.m_VoxelSize;
		const Vector3 minimum(
			static_cast<float>(bounds.m_Min.m_X) * voxelSize,
			static_cast<float>(bounds.m_Min.m_Y) * voxelSize,
			static_cast<float>(bounds.m_Min.m_Z) * voxelSize);
		const Vector3 maximum(
			static_cast<float>(bounds.m_MaxExclusive.m_X) * voxelSize,
			static_cast<float>(bounds.m_MaxExclusive.m_Y) * voxelSize,
			static_cast<float>(bounds.m_MaxExclusive.m_Z) * voxelSize);
		const Vector3 center = (minimum + maximum) * 0.5f;
		const Vector3 size = maximum - minimum;
		const float extent = std::max({ size.m_X, size.m_Y, size.m_Z, 1.0f });
		GetCamera().LookAt(center + Vector3(0.0f, extent * 0.25f, -extent * 1.8f), center);
		GetCamera().SetNearFar(0.05f, std::max(100.0f, extent * 20.0f));
		GetCamera().Update();
	}

	void NapaVoxelLabSession::ApplyImmediateParameters() noexcept
	{
		m_ShowChunkBounds = GetParameters().Get(ShowChunkBoundsId, true);
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->SetChannelEnabled(ChunkBoundsChannel, m_ShowChunkBounds);
			if (!m_ShowChunkBounds)
			{
				debugDraw->ClearChannel(ChunkBoundsChannel);
			}
		}
	}

	void NapaVoxelLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		using namespace napa::voxel;
		LogicalDomainMetrics metrics{};
		const bool configValid = ComputeLogicalDomainMetrics(m_CurrentConfig, metrics).Succeeded();
		const bool visible = m_PublicationSession && m_PublicationSession->HasVisibleMeshes();
		const WorldMeshValidationResult meshValidation = visible
			? m_PublicationSession->GetVisibleCoreMeshes().GetWorldMeshValidation()
			: WorldMeshValidationResult{};
		const BoundaryContourValidationResult boundaryValidation = visible
			? m_PublicationSession->GetVisibleCoreMeshes().GetBoundaryValidation()
			: BoundaryContourValidationResult{};

		diagnostics.m_Title = "Napa Voxel Static Viewer";
		diagnostics.m_Metrics = {
			{ .m_Name = "Preset", .m_Value = m_PresetName },
			{ .m_Name = "Config",
				.m_Value = std::format("{} cells/chunk, {:.3f} m voxel, {:.2f} surface band",
					m_CurrentConfig.m_ChunkCellCount, m_CurrentConfig.m_VoxelSize,
					m_CurrentConfig.m_SurfaceBandVoxels) },
			{ .m_Name = "Logical domain",
				.m_Value = configValid
					? std::format("{} cells, {} samples", metrics.m_TotalCellCount,
						metrics.m_TotalSampleCount)
					: "invalid" },
			{ .m_Name = "Resident / meshed chunks",
				.m_Value = std::format("{} / {}", m_VoxelWorld
					? m_VoxelWorld->GetResidentChunkCount()
					: 0, meshValidation.m_ChunkCount) },
			{ .m_Name = "World / visible revision",
				.m_Value = std::format("{} / {}", m_VoxelWorld
					? m_VoxelWorld->GetWorldVoxelRevision()
					: 0, m_PublicationSession
					? m_PublicationSession->GetVisibleWorldRevision()
					: 0) },
			{ .m_Name = "Voxel / mesh hash",
				.m_Value = std::format("{:016X} / {:016X}", m_InitialVoxelHash,
					meshValidation.m_ValidationHash) },
			{ .m_Name = "Geometry",
				.m_Value = std::format("{} vertices, {} indices, {} sections",
					meshValidation.m_VertexCount, meshValidation.m_IndexCount,
					meshValidation.m_SectionCount) },
			{ .m_Name = "Preparation timing",
				.m_Value = std::format("generation {:.3f} ms, mesh/validation {:.3f} ms",
					m_LastGenerationMilliseconds, m_LastMeshingMilliseconds) },
			{ .m_Name = "Publication",
				.m_Value = m_PublicationSession
					? GetPublicationStatusName(m_PublicationSession->GetPublicationStatus())
					: "Unavailable" },
			{ .m_Name = "Surface mode", .m_Value = "Shaded" },
		};
		diagnostics.m_Checks = {
			{
				.m_Name = "Atomic CPU/GPU publication",
				.m_Status = visible ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Core and GPU frame views become visible together after Copy Fence completion.",
			},
			{
				.m_Name = "Complete chunk domain",
				.m_Status = visible && configValid &&
					meshValidation.m_ChunkCount == metrics.m_CellOwnerChunkCount
					? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Visible records include every Cell-owner Chunk, including empty meshes.",
			},
			{
				.m_Name = "Boundary contour validation",
				.m_Status = visible &&
					boundaryValidation.m_ChunkRecordCount == meshValidation.m_ChunkCount
					? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Adjacent chunk contours satisfy the deterministic seam contract.",
			},
		};
	}

	LabId NapaVoxelLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.napa_voxel");
	}

	LabDescriptor NapaVoxelLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Napa Voxel",
			.m_Category = "Napa / Voxel",
			.m_Description =
				"Validates static Napa voxel generation, mesh publication, upload, and rendering.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 2,
		};
	}

	std::unique_ptr<LabSessionBase> NapaVoxelLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<NapaVoxelLabSession>(createInfo);
	}
}
