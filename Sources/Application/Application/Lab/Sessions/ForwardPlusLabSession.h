#pragma once

#include "Application/AssetPreparationTracker.h"
#include "Application/Lab/LabSessionBase.h"

#include <memory>

namespace gglab
{
	class ForwardPlusDebugReadback;

	class ForwardPlusLabSession final : public LabSessionBase
	{
	public:
		explicit ForwardPlusLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~ForwardPlusLabSession() override = default;

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
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		ForwardPlusLabSession(const LabSessionCreateInfo& createInfo,
			std::shared_ptr<ForwardPlusDebugReadback> debugReadback) noexcept;
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		void BuildScene() noexcept;
		void BuildLighting() noexcept;
		void ApplyCameraPreset() noexcept;
		void UpdateSelectedTile() noexcept;
		void CaptureGpuTimings() noexcept;
		void ArmGpuTimingCaptureWarmup() noexcept;

		std::shared_ptr<ForwardPlusDebugReadback> m_DebugReadback;
		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		uint64_t m_LastGpuProfileFrame = 0;
		uint32_t m_GpuTimingWarmupFrames = 0;
		bool m_EnableCameraInput = false;
		bool m_FixtureConfigured = false;
		bool m_GpuProfilerWasEnabled = false;
	};
}
