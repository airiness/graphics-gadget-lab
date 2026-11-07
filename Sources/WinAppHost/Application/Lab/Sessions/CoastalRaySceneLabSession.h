#pragma once
#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"

#include <memory>

namespace gglab
{
	struct CoastalRaySceneLabState;
	class CoastalRaySceneLabSession final : public LabSessionBase
	{
	public:
		explicit CoastalRaySceneLabSession(const LabSessionCreateInfo& info) noexcept;
		~CoastalRaySceneLabSession() override = default;
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_Progress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;
		std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept override;
		std::optional<FrameCaptureAssetBundle> GetCaptureAssetBundle() const noexcept override
		{
			return m_CaptureAssetBundle;
		}
		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& info) noexcept;

	protected:
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;

	private:
		std::shared_ptr<CoastalRaySceneLabState> m_State;
		ModelID m_Model;
		LoadingProgress m_Progress;
		std::optional<FrameCaptureAssetBundle> m_CaptureAssetBundle;
		EnvironmentLightingSettings m_PreviousEnvironment;
		bool m_Entered = false;
	};
}
