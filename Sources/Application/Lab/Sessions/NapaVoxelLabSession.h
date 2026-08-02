#pragma once

#include "Application/Lab/LabSessionBase.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderExtension.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "NapaVoxelCore/World/VoxelWorld.h"

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
		void Update(float deltaTime) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		NapaVoxelLabSession(const LabSessionCreateInfo& createInfo,
			std::shared_ptr<NapaVoxelRenderFrameSource> frameSource) noexcept;

		[[nodiscard]] bool PrepareInitialPublication() noexcept;
		void ScheduleInitialUpload() noexcept;
		void UpdatePreparationState() noexcept;
		void ApplyCameraPreset() noexcept;

		std::shared_ptr<NapaVoxelRenderFrameSource> m_FrameSource;
		std::unique_ptr<napa::voxel::VoxelWorld> m_VoxelWorld;
		std::shared_ptr<NapaVoxelInitialPublicationOwner> m_InitialPublication;
		NapaVoxelRenderState m_RenderState;
		LoadingProgress m_LoadingProgress{};
	};
}
