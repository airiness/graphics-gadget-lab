#pragma once

#include "Application/Lab/LabSessionBase.h"
#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderExtension.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <optional>
#include <string>

namespace gglab
{
	enum class NapaVoxelRuntimeState : uint8_t
	{
		Ready,
		Mutating,
		Meshing,
		Uploading,
		Publishing,
		Failed,
		Exiting,
	};

	class NapaVoxelLabSession final : public LabSessionBase
	{
	public:
		explicit NapaVoxelLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~NapaVoxelLabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_LoadingProgress;
		}
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		NapaVoxelLabSession(const LabSessionCreateInfo& createInfo,
			std::shared_ptr<NapaVoxelRenderFrameSource> frameSource) noexcept;

		[[nodiscard]] bool PrepareInitialPublication() noexcept;
		[[nodiscard]] bool PrepareInitialPublicationInternal();
		void CaptureInputCommands() noexcept;
		void AdvanceRuntime() noexcept;
		[[nodiscard]] bool ExecuteCommand(const NapaVoxelDequeuedCommand& command) noexcept;
		[[nodiscard]] bool ExecuteEdit(const napa::voxel::SphereEditRequest& request,
			uint64_t operationSerial) noexcept;
		[[nodiscard]] bool ExecuteMutation(napa::voxel::VoxelMutationResult mutation,
			const std::optional<napa::voxel::SphereEditRequest>& brush,
			uint64_t operationSerial) noexcept;
		[[nodiscard]] bool BuildCursorRay(NapaVoxelRay& ray) const noexcept;
		[[nodiscard]] napa::voxel::SphereEditRequest BuildBoundaryShot() const noexcept;
		void FailRuntime() noexcept;
		void PublishVisibleDebugState() noexcept;
		void UpdatePreparationState() noexcept;
		void DrawChunkBounds() noexcept;
		void DrawRuntimeDebug() noexcept;
		void ApplyCameraPreset() noexcept;
		void ApplyImmediateParameters() noexcept override;

		std::shared_ptr<NapaVoxelRenderFrameSource> m_FrameSource;
		NapaVoxelCommandQueue m_CommandQueue;
		std::unique_ptr<napa::voxel::VoxelWorld> m_VoxelWorld;
		std::unique_ptr<NapaVoxelPublicationSession> m_PublicationSession;
		napa::voxel::VoxelWorldConfig m_CurrentConfig{};
		napa::voxel::VoxelMutationResult m_PendingMutation{};
		napa::voxel::VoxelMutationResult m_VisibleDebugMutation{};
		std::optional<napa::voxel::SphereEditRequest> m_PendingBrush;
		std::optional<napa::voxel::SphereEditRequest> m_VisibleDebugBrush;
		std::optional<napa::voxel::ChunkCoord> m_ProbeChunk;
		std::optional<napa::voxel::Double3> m_ProbePosition;
		LoadingProgress m_LoadingProgress{};
		std::string m_PresetName = "Single Chunk";
		uint64_t m_InitialVoxelHash = 0;
		uint64_t m_AuthoritativeVoxelHash = 0;
		uint64_t m_ActiveOperationSerial = 0;
		uint32_t m_WindowWidth = 1;
		uint32_t m_WindowHeight = 1;
		NapaVoxelRuntimeState m_RuntimeState = NapaVoxelRuntimeState::Ready;
		NapaVoxelSurfaceMode m_SurfaceMode = NapaVoxelSurfaceMode::Shaded;
		double m_LastGenerationMilliseconds = 0.0;
		double m_LastMeshingMilliseconds = 0.0;
		double m_LastEditMilliseconds = 0.0;
		bool m_ShowChunkBounds = true;
		bool m_ShowDirtyChunks = true;
		bool m_ShowDamageMarkers = true;
	};
}
