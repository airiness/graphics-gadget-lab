#pragma once

#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <memory>

namespace gglab
{
	class AtmosphereRangeLabSession final : public LabSessionBase
	{
	public:
		explicit AtmosphereRangeLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AtmosphereRangeLabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_LoadingProgress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		ModelID m_PendingModelId{};
		LoadingProgress m_LoadingProgress{};
		EnvironmentLightingSettings m_PreviousEnvironment{};
		bool m_HasEnvironmentOverride = false;
	};
}
