#include "Application/Lab/Sessions/NapaVoxelLabSession.h"

#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Core/Math/MathFunctions.h"
#include "Graphics/Camera.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/Edit/VoxelDamage.h"
#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/World/VoxelRestore.h"
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
		const LabParameterId BrushRadiusId("napa_voxel.brush.radius");
		const LabParameterId BrushStrengthId("napa_voxel.brush.strength");
		const LabParameterId DamagePerHitId("napa_voxel.brush.damage_per_hit");
		const LabParameterId StoneThresholdId("napa_voxel.brush.stone_threshold");
		const LabParameterId ShowDirtyChunksId("napa_voxel.debug.show_dirty_chunks");
		const LabParameterId ShowDamageMarkersId("napa_voxel.debug.show_damage_markers");
		const StringID ChunkBoundsChannel("NapaVoxel.ChunkBounds");
		const StringID DirtyChunksChannel("NapaVoxel.DirtyChunks");
		const StringID EditBoundsChannel("NapaVoxel.EditBounds");
		const StringID BrushChannel("NapaVoxel.Brush");
		const StringID DamageChannel("NapaVoxel.Damage");
		const StringID SeamFailureChannel("NapaVoxel.SeamFailure");

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

		[[nodiscard]] const char* GetRuntimeStateName(
			NapaVoxelRuntimeState state) noexcept
		{
			switch (state)
			{
			case NapaVoxelRuntimeState::Ready:
				return "Ready";
			case NapaVoxelRuntimeState::Mutating:
				return "Mutating";
			case NapaVoxelRuntimeState::Meshing:
				return "Meshing";
			case NapaVoxelRuntimeState::Uploading:
				return "Uploading";
			case NapaVoxelRuntimeState::Publishing:
				return "Publishing";
			case NapaVoxelRuntimeState::Failed:
				return "Failed";
			case NapaVoxelRuntimeState::Exiting:
				return "Exiting";
			}
			return "Unknown";
		}

		[[nodiscard]] const char* GetSurfaceModeName(NapaVoxelSurfaceMode mode) noexcept
		{
			return mode == NapaVoxelSurfaceMode::Wireframe ? "Wireframe" : "Shaded";
		}

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
		m_WindowWidth = createInfo.m_WindowWidth;
		m_WindowHeight = createInfo.m_WindowHeight;
		auto* renderer = m_Services.m_Renderer;
		m_PublicationSession = std::make_unique<NapaVoxelPublicationSession>(
			renderer ? renderer->GetDevice() : nullptr,
			renderer ? renderer->GetAssetUploadScheduler() : nullptr, &m_CommandQueue);

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
			.m_EditPolicy = LabParameterEditPolicy::CommitOnEditEnd,
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
			.m_EditPolicy = LabParameterEditPolicy::CommitOnEditEnd,
			.m_DefaultValue = 2.0f,
			.m_MinValue = 1.0f,
			.m_MaxValue = 4.0f,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BrushRadiusId,
			.m_Name = "Radius",
			.m_Group = "Brush",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.25f,
			.m_MinValue = 0.05f,
			.m_MaxValue = 8.0f,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BrushStrengthId,
			.m_Name = "Strength",
			.m_Group = "Brush",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = 0.0f,
			.m_MaxValue = 1.0f,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = DamagePerHitId,
			.m_Name = "Damage Per Hit",
			.m_Group = "Brush",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t{ 128 },
			.m_MinValue = uint32_t{ 0 },
			.m_MaxValue = uint32_t{ 255 },
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = StoneThresholdId,
			.m_Name = "Stone Threshold",
			.m_Group = "Brush",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t{ 255 },
			.m_MinValue = uint32_t{ 1 },
			.m_MaxValue = uint32_t{ 255 },
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
			.m_Id = ShowDirtyChunksId,
			.m_Name = "Show Dirty Chunks",
			.m_Group = "Debug",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ShowDamageMarkersId,
			.m_Name = "Show Damage Markers",
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
			.m_DefaultValue = static_cast<int32_t>(NapaVoxelSurfaceMode::Shaded),
			.m_EnumItems = {
				{ static_cast<int32_t>(NapaVoxelSurfaceMode::Shaded), "Shaded" },
				{ static_cast<int32_t>(NapaVoxelSurfaceMode::Wireframe), "Wireframe" },
			},
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
		m_RuntimeState = NapaVoxelRuntimeState::Ready;
		m_InitialVoxelHash = 0;
		m_LastGenerationMilliseconds = 0.0;
		m_LastMeshingMilliseconds = 0.0;
		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.1f,
			.m_Stage = "Building initial voxel publication",
			.m_Detail = "Generating and validating the selected CPU mesh set.",
		};
		if (!PrepareInitialPublication())
		{
			CancelPrepare();
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.0f,
				.m_Stage = "Initial voxel preparation failed",
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
		m_RuntimeState = NapaVoxelRuntimeState::Exiting;
		m_CommandQueue.ResetForNewSession();
		if (m_PublicationSession)
		{
			m_PublicationSession->CancelPrepare();
		}
		if (m_FrameSource)
		{
			m_FrameSource->ClearFrameView();
		}
		m_VoxelWorld.reset();
		m_PendingMutation = {};
		m_VisibleDebugMutation = {};
		m_PendingBrush.reset();
		m_VisibleDebugBrush.reset();
		m_ProbeChunk.reset();
		m_ProbePosition.reset();
		m_ActiveOperationSerial = 0;
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void NapaVoxelLabSession::OnEnter() noexcept
	{
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->SetChannelEnabled(ChunkBoundsChannel, m_ShowChunkBounds);
			debugDraw->SetChannelEnabled(DirtyChunksChannel, m_ShowDirtyChunks);
			debugDraw->SetChannelEnabled(EditBoundsChannel, true);
			debugDraw->SetChannelEnabled(BrushChannel, true);
			debugDraw->SetChannelEnabled(DamageChannel, m_ShowDamageMarkers);
			debugDraw->SetChannelEnabled(SeamFailureChannel, true);
		}
	}

	void NapaVoxelLabSession::OnExit() noexcept
	{
		m_RuntimeState = NapaVoxelRuntimeState::Exiting;
		m_CommandQueue.ResetForNewSession();
		if (m_PublicationSession)
		{
			m_PublicationSession->CancelDynamicPrepare();
		}
		if (m_FrameSource)
		{
			m_FrameSource->ClearFrameView();
		}
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->ClearChannel(ChunkBoundsChannel);
			debugDraw->ClearChannel(DirtyChunksChannel);
			debugDraw->ClearChannel(EditBoundsChannel);
			debugDraw->ClearChannel(BrushChannel);
			debugDraw->ClearChannel(DamageChannel);
			debugDraw->ClearChannel(SeamFailureChannel);
		}
	}

	void NapaVoxelLabSession::Update(float deltaTime) noexcept
	{
		UpdateCamera(deltaTime);
		CaptureInputCommands();
		AdvanceRuntime();
		if (m_PublicationSession)
		{
			m_PublicationSession->RetireCompletedGpuMeshes();
		}
		DrawChunkBounds();
		DrawRuntimeDebug();
	}

	void NapaVoxelLabSession::OnFrameSubmitted(
		const DemoFrameFeedback& feedback) noexcept
	{
		if (m_PublicationSession)
		{
			m_PublicationSession->OnFrameSubmitted(feedback.m_SubmittedFence);
		}
	}

	void NapaVoxelLabSession::OnResize(uint32_t width, uint32_t height) noexcept
	{
		LabSessionBase::OnResize(width, height);
		if (width > 0 && height > 0)
		{
			m_WindowWidth = width;
			m_WindowHeight = height;
		}
	}

	void NapaVoxelLabSession::CaptureInputCommands() noexcept
	{
		if (m_RuntimeState == NapaVoxelRuntimeState::Failed ||
			m_RuntimeState == NapaVoxelRuntimeState::Exiting ||
			!m_Services.m_InputManager)
		{
			return;
		}

		auto* keyboard = m_Services.m_InputManager->GetKeyboard();
		auto* mouse = m_Services.m_InputManager->GetMouse();
		if (!keyboard || !mouse)
		{
			return;
		}

		const bool keyboardCapturedByUI = m_Services.m_InputManager->IsKeyboardCapturedByUI();
		if (!keyboardCapturedByUI)
		{
			if (keyboard->IsKeyPressed(KeyCode::B))
			{
				m_ShowChunkBounds = !m_ShowChunkBounds;
				GGLAB_UNUSED(GetMutableParameters().Set(ShowChunkBoundsId, m_ShowChunkBounds));
			}
			if (keyboard->IsKeyPressed(KeyCode::M))
			{
				m_SurfaceMode = m_SurfaceMode == NapaVoxelSurfaceMode::Shaded
					? NapaVoxelSurfaceMode::Wireframe
					: NapaVoxelSurfaceMode::Shaded;
				GGLAB_UNUSED(GetMutableParameters().Set(
					SurfaceModeId, static_cast<int32_t>(m_SurfaceMode)));
				if (m_FrameSource)
				{
					m_FrameSource->SetSurfaceMode(m_SurfaceMode);
				}
			}

			if (keyboard->IsKeyPressed(KeyCode::R))
			{
				GGLAB_UNUSED(m_CommandQueue.EnqueueRestoreAll());
			}
			if (keyboard->IsKeyPressed(KeyCode::C) && m_ProbeChunk)
			{
				GGLAB_UNUSED(m_CommandQueue.EnqueueRestoreProbeChunk(*m_ProbeChunk));
			}
			if (keyboard->IsKeyPressed(KeyCode::Space))
			{
				GGLAB_UNUSED(m_CommandQueue.EnqueueScriptedBoundaryShot(BuildBoundaryShot()));
			}
		}

		if (m_Services.m_InputManager->IsMouseCapturedByUI() ||
			!mouse->IsMouseButtonPressed(MouseButton::LeftButton))
		{
			return;
		}
		NapaVoxelRay ray{};
		if (!BuildCursorRay(ray))
		{
			return;
		}
		const bool moveProbe = !keyboardCapturedByUI &&
			(keyboard->IsKeyHeld(KeyCode::LeftShift) ||
				keyboard->IsKeyHeld(KeyCode::RightShift));
		if (moveProbe)
		{
			GGLAB_UNUSED(m_CommandQueue.EnqueueMoveProbeRay(ray));
			return;
		}

		const uint32_t damagePerHit = GetParameters().Get(DamagePerHitId, uint32_t{ 128 });
		const uint32_t stoneThreshold = GetParameters().Get(StoneThresholdId, uint32_t{ 255 });
		const NapaVoxelFireParameters parameters{
			.m_Radius = static_cast<double>(GetParameters().Get(BrushRadiusId, 1.25f)),
			.m_Strength = static_cast<double>(GetParameters().Get(BrushStrengthId, 1.0f)),
			.m_MaterialRules = {
				.m_DamagePerHit = static_cast<uint8_t>(damagePerHit),
				.m_StoneBreakThreshold = static_cast<uint8_t>(stoneThreshold),
			},
		};
		GGLAB_UNUSED(m_CommandQueue.EnqueueFireRay(ray, parameters));
	}

	void NapaVoxelLabSession::AdvanceRuntime() noexcept
	{
		if (!m_PublicationSession || !m_VoxelWorld ||
			m_RuntimeState == NapaVoxelRuntimeState::Failed ||
			m_RuntimeState == NapaVoxelRuntimeState::Exiting)
		{
			return;
		}

		if (m_RuntimeState == NapaVoxelRuntimeState::Uploading)
		{
			if (m_PublicationSession->TickMeshPrepare())
			{
				m_RuntimeState = NapaVoxelRuntimeState::Publishing;
				PublishVisibleDebugState();
				m_FrameSource->SetFrameView(m_PublicationSession->GetFrameView());
				m_RuntimeState = NapaVoxelRuntimeState::Ready;
			}
			else if (m_PublicationSession->HasFailed())
			{
				FailRuntime();
			}
			return;
		}

		if (m_RuntimeState != NapaVoxelRuntimeState::Ready ||
			!m_PublicationSession->CanPublishDynamic())
		{
			return;
		}

		NapaVoxelDequeuedCommand command{};
		const NapaVoxelCommandQueueError dequeueResult = m_CommandQueue.Dequeue(command);
		if (dequeueResult == NapaVoxelCommandQueueError::Empty)
		{
			return;
		}
		if (dequeueResult != NapaVoxelCommandQueueError::None || !ExecuteCommand(command))
		{
			FailRuntime();
		}
	}

	bool NapaVoxelLabSession::ExecuteCommand(const NapaVoxelDequeuedCommand& command) noexcept
	{
		using namespace napa::voxel;
		m_ActiveOperationSerial = command.m_OperationSerial;
		m_RuntimeState = NapaVoxelRuntimeState::Mutating;

		if (const auto* fire = std::get_if<NapaVoxelFireRayCommand>(&command.m_Command.m_Data))
		{
			NapaVoxelRaycastHit hit{};
			const NapaVoxelRaycastResult raycast = RaycastNapaVoxelVisibleMesh(
				m_PublicationSession->GetVisibleCoreMeshes(), fire->m_Ray, hit);
			if (raycast.Failed())
			{
				return false;
			}
			if (!raycast.m_Hit)
			{
				m_RuntimeState = NapaVoxelRuntimeState::Ready;
				return true;
			}

			SphereEditRequest request{};
			if (PrepareNapaVoxelFireEditRequest(*fire, hit.m_WorldPosition, request).Failed())
			{
				return false;
			}
			return ExecuteEdit(request, command.m_OperationSerial);
		}

		if (const auto* moveProbe =
			std::get_if<NapaVoxelMoveProbeRayCommand>(&command.m_Command.m_Data))
		{
			NapaVoxelRaycastHit hit{};
			const NapaVoxelRaycastResult raycast = RaycastNapaVoxelVisibleMesh(
				m_PublicationSession->GetVisibleCoreMeshes(), moveProbe->m_Ray, hit);
			if (raycast.Failed())
			{
				return false;
			}
			if (raycast.m_Hit)
			{
				m_ProbeChunk = hit.m_Chunk;
				m_ProbePosition = hit.m_WorldPosition;
			}
			m_RuntimeState = NapaVoxelRuntimeState::Ready;
			return true;
		}

		VoxelMutationResult mutation{};
		if (std::holds_alternative<NapaVoxelRestoreAllCommand>(command.m_Command.m_Data))
		{
			const auto begin = std::chrono::steady_clock::now();
			const ValidationResult result = RestoreAll(*m_VoxelWorld, mutation);
			m_LastEditMilliseconds = Milliseconds(std::chrono::steady_clock::now() - begin);
			return result.Succeeded() && ExecuteMutation(
				std::move(mutation), std::nullopt, command.m_OperationSerial);
		}
		if (const auto* restoreChunk =
			std::get_if<NapaVoxelRestoreProbeChunkCommand>(&command.m_Command.m_Data))
		{
			const auto begin = std::chrono::steady_clock::now();
			const ValidationResult result = RestoreSampleOwnerChunk(
				*m_VoxelWorld, restoreChunk->m_Chunk, mutation);
			m_LastEditMilliseconds = Milliseconds(std::chrono::steady_clock::now() - begin);
			return result.Succeeded() && ExecuteMutation(
				std::move(mutation), std::nullopt, command.m_OperationSerial);
		}
		if (const auto* boundary =
			std::get_if<NapaVoxelScriptedBoundaryShotCommand>(&command.m_Command.m_Data))
		{
			return ExecuteEdit(boundary->m_Edit, command.m_OperationSerial);
		}
		return false;
	}

	bool NapaVoxelLabSession::ExecuteEdit(const napa::voxel::SphereEditRequest& request,
		uint64_t operationSerial) noexcept
	{
		napa::voxel::VoxelMutationResult mutation{};
		const auto begin = std::chrono::steady_clock::now();
		const napa::voxel::ValidationResult result =
			napa::voxel::ApplySphereEdit(*m_VoxelWorld, request, mutation);
		m_LastEditMilliseconds = Milliseconds(std::chrono::steady_clock::now() - begin);
		return result.Succeeded() && ExecuteMutation(std::move(mutation), request, operationSerial);
	}

	bool NapaVoxelLabSession::ExecuteMutation(napa::voxel::VoxelMutationResult mutation,
		const std::optional<napa::voxel::SphereEditRequest>& brush,
		uint64_t operationSerial) noexcept
	{
		using namespace napa::voxel;
		if (!mutation.Changed())
		{
			m_RuntimeState = NapaVoxelRuntimeState::Ready;
			return true;
		}

		try
		{
			auto damageSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			if (BuildVoxelDamageMarkerSnapshot(*m_VoxelWorld,
				mutation.m_TargetWorldVoxelRevision, *damageSnapshot).Failed() ||
				ComputeLogicalVoxelWorldHash(*m_VoxelWorld, m_AuthoritativeVoxelHash).Failed())
			{
				return false;
			}

			m_PendingMutation = mutation;
			m_PendingBrush = brush;
			if (mutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly)
			{
				m_RuntimeState = NapaVoxelRuntimeState::Publishing;
				if (!m_PublicationSession->PublishDataOnly(*m_VoxelWorld, mutation,
					operationSerial, InitialOwnerGeneration, damageSnapshot))
				{
					return false;
				}
				PublishVisibleDebugState();
				m_FrameSource->SetFrameView(m_PublicationSession->GetFrameView());
				m_RuntimeState = NapaVoxelRuntimeState::Ready;
				return true;
			}

			m_RuntimeState = NapaVoxelRuntimeState::Meshing;
			const auto meshingBegin = std::chrono::steady_clock::now();
			CpuMeshBatch batch{};
			if (BuildCpuMeshBatch(*m_VoxelWorld, mutation, batch).Failed())
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
			if (!m_PublicationSession->BeginMeshPrepare(pendingCoreMeshes,
				operationSerial, InitialOwnerGeneration, damageSnapshot))
			{
				return false;
			}
			m_RuntimeState = NapaVoxelRuntimeState::Uploading;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool NapaVoxelLabSession::BuildCursorRay(NapaVoxelRay& ray) const noexcept
	{
		if (!m_Services.m_InputManager || m_WindowWidth == 0 || m_WindowHeight == 0)
		{
			return false;
		}
		const Mouse* mouse = m_Services.m_InputManager->GetMouse();
		if (!mouse)
		{
			return false;
		}
		Vector2 cursor(static_cast<float>(m_WindowWidth) * 0.5f,
			static_cast<float>(m_WindowHeight) * 0.5f);
		if (mouse->GetMouseMode() == Mouse::MouseMode::Absolute)
		{
			cursor = mouse->GetMouseCoord();
		}
		const float clipX = cursor.m_X / static_cast<float>(m_WindowWidth) * 2.0f - 1.0f;
		const float clipY = 1.0f - cursor.m_Y / static_cast<float>(m_WindowHeight) * 2.0f;
		Matrix inverseViewProjection{};
		if (!math::TryInverse(
			GetCamera().GetViewMatrix() * GetCamera().GetProjMatrix(), inverseViewProjection))
		{
			return false;
		}
		const Vector3 farPoint = math::TransformPoint(
			Vector3(clipX, clipY, 0.0f), inverseViewProjection);
		Vector3 direction{};
		if (!math::TryNormalize(farPoint - GetCamera().GetPosition(), direction))
		{
			return false;
		}
		const Vector3 origin = GetCamera().GetPosition();
		ray = {
			.m_Origin = { origin.m_X, origin.m_Y, origin.m_Z },
			.m_Direction = { direction.m_X, direction.m_Y, direction.m_Z },
		};
		return IsValidNapaVoxelRay(ray);
	}

	napa::voxel::SphereEditRequest NapaVoxelLabSession::BuildBoundaryShot() const noexcept
	{
		const auto& bounds = m_CurrentConfig.m_LogicalCellBounds;
		const int32_t chunkCells = static_cast<int32_t>(m_CurrentConfig.m_ChunkCellCount);
		const int32_t spanX = bounds.m_MaxExclusive.m_X - bounds.m_Min.m_X;
		const double boundaryX = static_cast<double>(spanX > chunkCells
			? bounds.m_Min.m_X + chunkCells
			: bounds.m_Min.m_X + spanX / 2);
		const double centerY = static_cast<double>(
			bounds.m_Min.m_Y + (bounds.m_MaxExclusive.m_Y - bounds.m_Min.m_Y) / 2);
		const double centerZ = static_cast<double>(
			bounds.m_Min.m_Z + (bounds.m_MaxExclusive.m_Z - bounds.m_Min.m_Z) / 2);
		const double voxelSize = static_cast<double>(m_CurrentConfig.m_VoxelSize);
		return {
			.m_Brush = {
				.m_CenterWorld = { boundaryX * voxelSize, centerY * voxelSize,
					centerZ * voxelSize },
				.m_Radius = static_cast<double>(GetParameters().Get(BrushRadiusId, 1.25f)),
				.m_Strength = static_cast<double>(GetParameters().Get(BrushStrengthId, 1.0f)),
			},
			.m_MaterialRules = {
				.m_DamagePerHit = static_cast<uint8_t>(
					GetParameters().Get(DamagePerHitId, uint32_t{ 128 })),
				.m_StoneBreakThreshold = static_cast<uint8_t>(
					GetParameters().Get(StoneThresholdId, uint32_t{ 255 })),
			},
		};
	}

	void NapaVoxelLabSession::FailRuntime() noexcept
	{
		m_RuntimeState = NapaVoxelRuntimeState::Failed;
		m_CommandQueue.Freeze(m_CommandQueue.GetTerminalError() ==
			NapaVoxelCommandQueueError::PublicationSerialExhausted
			? NapaVoxelCommandQueueError::PublicationSerialExhausted
			: NapaVoxelCommandQueueError::HostPreparationFailed);
	}

	void NapaVoxelLabSession::PublishVisibleDebugState() noexcept
	{
		m_VisibleDebugMutation = std::move(m_PendingMutation);
		m_VisibleDebugBrush = std::move(m_PendingBrush);
		m_PendingMutation = {};
		m_PendingBrush.reset();
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
		m_AuthoritativeVoxelHash = generation.m_InitialVoxelHash;

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
				.m_Stage = "Initial voxel publication failed",
				.m_Detail = "The prepared CPU/GPU publication could not be completed.",
			};
			return;
		}

		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.75f,
			.m_Stage = "Uploading initial voxel meshes",
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

	void NapaVoxelLabSession::DrawRuntimeDebug() noexcept
	{
		auto* debugDraw = m_Services.m_DebugDraw;
		if (!debugDraw || !m_PublicationSession || !m_PublicationSession->HasVisibleMeshes())
		{
			return;
		}

		const float voxelSize = m_CurrentConfig.m_VoxelSize;
		const float chunkSize = static_cast<float>(m_CurrentConfig.m_ChunkCellCount) * voxelSize;
		const Vector3 chunkExtents(chunkSize * 0.5f);
		const auto drawChunks = [&](std::span<const napa::voxel::ChunkCoord> chunks,
			const DebugDrawStyle& style) noexcept
			{
				for (napa::voxel::ChunkCoord chunk : chunks)
				{
					NapaVoxelWorldPosition origin{};
					Vector3 translation{};
					if (ComputeNapaVoxelChunkOrigin(m_CurrentConfig, chunk, origin).Succeeded() &&
						ComputeNapaVoxelRenderTranslation(
							m_CurrentConfig, origin, {}, translation).Succeeded())
					{
						debugDraw->Box(translation + chunkExtents, chunkExtents, style);
					}
				}
			};

		if (m_ShowDirtyChunks)
		{
			drawChunks(m_VisibleDebugMutation.m_DataDirtyChunks, {
				.m_Color = Color::Yellow,
				.m_Channel = DirtyChunksChannel,
				});
			drawChunks(m_VisibleDebugMutation.m_MeshDirtyChunks, {
				.m_Color = Color::Orange,
				.m_Channel = DirtyChunksChannel,
				});
		}

		if (m_VisibleDebugBrush)
		{
			const auto& brush = m_VisibleDebugBrush->m_Brush;
			const Vector3 center(static_cast<float>(brush.m_CenterWorld.m_X),
				static_cast<float>(brush.m_CenterWorld.m_Y),
				static_cast<float>(brush.m_CenterWorld.m_Z));
			debugDraw->Sphere(center, static_cast<float>(brush.m_Radius), {
				.m_Color = Color::Red,
				.m_Channel = BrushChannel,
				});

			napa::voxel::SphereEditContext context{};
			if (napa::voxel::PrepareSphereEditContext(
				m_CurrentConfig, *m_VisibleDebugBrush, context).Succeeded())
			{
				const napa::voxel::SampleAabb bounds = context.GetScanBounds();
				const Vector3 minimum(
					static_cast<float>(bounds.m_Min.m_X) * voxelSize,
					static_cast<float>(bounds.m_Min.m_Y) * voxelSize,
					static_cast<float>(bounds.m_Min.m_Z) * voxelSize);
				const Vector3 maximum(
					static_cast<float>(bounds.m_MaxExclusive.m_X) * voxelSize,
					static_cast<float>(bounds.m_MaxExclusive.m_Y) * voxelSize,
					static_cast<float>(bounds.m_MaxExclusive.m_Z) * voxelSize);
				debugDraw->Box((minimum + maximum) * 0.5f, (maximum - minimum) * 0.5f, {
					.m_Color = Color::Magenta,
					.m_Channel = EditBoundsChannel,
					});
			}
		}

		if (m_ShowDamageMarkers)
		{
			if (const auto* damage = m_PublicationSession->GetVisibleDamageSnapshot())
			{
				const float markerSize = std::max(voxelSize * 0.18f, 0.02f);
				for (const napa::voxel::VoxelDamageMarker& marker : damage->m_Markers)
				{
					debugDraw->Point({
						static_cast<float>(marker.m_WorldPosition.m_X),
						static_cast<float>(marker.m_WorldPosition.m_Y),
						static_cast<float>(marker.m_WorldPosition.m_Z),
						}, markerSize, {
						.m_Color = Color::Gold,
						.m_DepthMode = DebugDrawDepthMode::Always,
						.m_Channel = DamageChannel,
						});
				}
			}
		}

		if (m_ProbePosition)
		{
			debugDraw->Point({
				static_cast<float>(m_ProbePosition->m_X),
				static_cast<float>(m_ProbePosition->m_Y),
				static_cast<float>(m_ProbePosition->m_Z),
				}, std::max(voxelSize * 0.35f, 0.04f), {
				.m_Color = Color::Green,
				.m_DepthMode = DebugDrawDepthMode::Always,
				.m_Channel = BrushChannel,
				});
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
		m_ShowDirtyChunks = GetParameters().Get(ShowDirtyChunksId, true);
		m_ShowDamageMarkers = GetParameters().Get(ShowDamageMarkersId, true);
		m_SurfaceMode = static_cast<NapaVoxelSurfaceMode>(GetParameters().Get(
			SurfaceModeId, static_cast<int32_t>(NapaVoxelSurfaceMode::Shaded)));
		if (m_SurfaceMode != NapaVoxelSurfaceMode::Shaded &&
			m_SurfaceMode != NapaVoxelSurfaceMode::Wireframe)
		{
			m_SurfaceMode = NapaVoxelSurfaceMode::Shaded;
		}
		if (m_FrameSource)
		{
			m_FrameSource->SetSurfaceMode(m_SurfaceMode);
		}
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->SetChannelEnabled(ChunkBoundsChannel, m_ShowChunkBounds);
			debugDraw->SetChannelEnabled(DirtyChunksChannel, m_ShowDirtyChunks);
			debugDraw->SetChannelEnabled(DamageChannel, m_ShowDamageMarkers);
			if (!m_ShowChunkBounds)
			{
				debugDraw->ClearChannel(ChunkBoundsChannel);
			}
			if (!m_ShowDirtyChunks)
			{
				debugDraw->ClearChannel(DirtyChunksChannel);
			}
			if (!m_ShowDamageMarkers)
			{
				debugDraw->ClearChannel(DamageChannel);
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
		const GGLabMeshPublicationIdentity pendingIdentity = m_PublicationSession
			? m_PublicationSession->GetPendingIdentity()
			: GGLabMeshPublicationIdentity{};
		const auto* damageSnapshot = m_PublicationSession
			? m_PublicationSession->GetVisibleDamageSnapshot()
			: nullptr;

		diagnostics.m_Title = "Napa Voxel Interactive Destruction";
		diagnostics.m_Metrics = {
			{.m_Name = "Preset", .m_Value = m_PresetName },
			{.m_Name = "Config",
				.m_Value = std::format("{} cells/chunk, {:.3f} m voxel, {:.2f} surface band",
					m_CurrentConfig.m_ChunkCellCount, m_CurrentConfig.m_VoxelSize,
					m_CurrentConfig.m_SurfaceBandVoxels) },
			{.m_Name = "Logical domain",
				.m_Value = configValid
					? std::format("{} cells, {} samples", metrics.m_TotalCellCount,
						metrics.m_TotalSampleCount)
					: "invalid" },
			{.m_Name = "Resident / meshed chunks",
				.m_Value = std::format("{} / {}", m_VoxelWorld
					? m_VoxelWorld->GetResidentChunkCount()
					: 0, meshValidation.m_ChunkCount) },
			{.m_Name = "World / visible revision",
				.m_Value = std::format("{} / {}", m_VoxelWorld
					? m_VoxelWorld->GetWorldVoxelRevision()
					: 0, m_PublicationSession
					? m_PublicationSession->GetVisibleWorldRevision()
					: 0) },
			{.m_Name = "Voxel / mesh hash",
				.m_Value = std::format("{:016X} / {:016X}", m_AuthoritativeVoxelHash,
					meshValidation.m_ValidationHash) },
			{.m_Name = "Geometry",
				.m_Value = std::format("{} vertices, {} indices, {} sections",
					meshValidation.m_VertexCount, meshValidation.m_IndexCount,
					meshValidation.m_SectionCount) },
			{.m_Name = "Runtime",
				.m_Value = std::format("{}, queue {}, operation {}", GetRuntimeStateName(m_RuntimeState),
					m_CommandQueue.GetSize(), m_ActiveOperationSerial) },
			{.m_Name = "Pending publication",
				.m_Value = pendingIdentity.m_PublicationSerial != 0
					? std::format("base {} -> target {}, serial {}",
						m_PendingMutation.m_BaseWorldVoxelRevision,
						m_PendingMutation.m_TargetWorldVoxelRevision,
						pendingIdentity.m_PublicationSerial)
					: "none" },
			{.m_Name = "Dirty chunks",
				.m_Value = std::format("data {}, mesh {}",
					m_VisibleDebugMutation.m_DataDirtyChunks.size(),
					m_VisibleDebugMutation.m_MeshDirtyChunks.size()) },
			{.m_Name = "Damage markers",
				.m_Value = damageSnapshot
					? std::format("{} visible / {} total{}", damageSnapshot->m_Markers.size(),
						damageSnapshot->m_TotalDamagedSampleCount,
						damageSnapshot->m_Truncated ? " (truncated)" : "")
					: "unavailable" },
			{.m_Name = "Timing",
				.m_Value = std::format("generation {:.3f} ms, edit {:.3f} ms, mesh {:.3f} ms",
					m_LastGenerationMilliseconds, m_LastEditMilliseconds,
					m_LastMeshingMilliseconds) },
			{.m_Name = "Publication",
				.m_Value = std::format("visible serial {}, retired sets {}",
					m_PublicationSession ? m_PublicationSession->GetLastPublicationSerial() : 0,
					m_PublicationSession
						? m_PublicationSession->GetRetiredGpuMeshSetCount()
						: 0) },
			{.m_Name = "Surface mode", .m_Value = GetSurfaceModeName(m_SurfaceMode) },
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
			{
				.m_Name = "Authoritative / visible coherence",
				.m_Status = visible && m_RuntimeState == NapaVoxelRuntimeState::Ready &&
					m_VoxelWorld->GetWorldVoxelRevision() ==
					m_PublicationSession->GetVisibleWorldRevision()
					? LabDiagnosticCheckStatus::Passed
					: m_RuntimeState == NapaVoxelRuntimeState::Failed
						? LabDiagnosticCheckStatus::Failed
						: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Visible CPU, GPU, and debug snapshots publish one complete revision.",
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
				"Validates interactive Napa voxel destruction, restore, publication, and rendering.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 3,
		};
	}

	std::unique_ptr<LabSessionBase> NapaVoxelLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<NapaVoxelLabSession>(createInfo);
	}
}
