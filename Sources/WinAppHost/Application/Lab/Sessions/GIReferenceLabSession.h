#pragma once
#include "AssetPreparationTracker.h"
#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingOverrideBase.h"

#include <filesystem>

namespace gglab
{
	class GIReferenceLabSession final : public LabSessionBase
	{
	public:
		explicit GIReferenceLabSession(const LabSessionCreateInfo& info) noexcept;
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_Progress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& info) noexcept;

	private:
		AssetPreparationTracker m_Assets;
		LoadingProgress m_Progress;
		std::unique_ptr<EnvironmentLightingOverrideBase> m_LightingOverride;
		std::filesystem::path m_PreviousEnvironmentFile;
		bool m_SelectedEnvironment = false;
	};
}
