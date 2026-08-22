#pragma once
#include "AssetPreparationTracker.h"
#include "Lab/LabSessionBase.h"

namespace gglab
{
	class AlphaTestLabSession final : public LabSessionBase
	{
	public:
		explicit AlphaTestLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AlphaTestLabSession() override = default;

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
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		void BuildLighting() noexcept;
		void ApplyCameraPreset() noexcept;

		bool m_EnableCameraInput = true;
		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
	};
}
