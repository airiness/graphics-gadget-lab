#pragma once

#include "Application/Lab/LabSessionBase.h"
#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderExtension.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "NapaVoxelCore/World/VoxelWorld.h"

#include <string>

namespace gglab
{
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
		void UpdatePreparationState() noexcept;
		void DrawChunkBounds() noexcept;
		void ApplyCameraPreset() noexcept;
		void ApplyImmediateParameters() noexcept override;

		std::shared_ptr<NapaVoxelRenderFrameSource> m_FrameSource;
		NapaVoxelCommandQueue m_CommandQueue;
		std::unique_ptr<napa::voxel::VoxelWorld> m_VoxelWorld;
		std::unique_ptr<NapaVoxelStaticPublicationSession> m_PublicationSession;
		napa::voxel::VoxelWorldConfig m_CurrentConfig{};
		LoadingProgress m_LoadingProgress{};
		std::string m_PresetName = "Single Chunk";
		uint64_t m_InitialVoxelHash = 0;
		double m_LastGenerationMilliseconds = 0.0;
		double m_LastMeshingMilliseconds = 0.0;
		bool m_ShowChunkBounds = true;
	};
}
