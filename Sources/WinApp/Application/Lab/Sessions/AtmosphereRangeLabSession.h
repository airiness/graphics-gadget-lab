#pragma once

#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingOverrideBase.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <memory>
#include <optional>

namespace gglab
{
	class AtmosphereRangeLabSession final : public LabSessionBase
	{
	public:
		explicit AtmosphereRangeLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AtmosphereRangeLabSession() override = default;

		std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept override;
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_LoadingProgress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		[[nodiscard]] bool SupportsImmediateParameter(const LabParameterId& id) const noexcept override;
		[[nodiscard]] std::optional<LabValue> ReadImmediateParameterFrameValue(
			const LabParameterId& id, const LabParameterFrameContext& frame) const noexcept override;
		[[nodiscard]] bool SupportsDeferredLightingParameter(const LabParameterId& id) const noexcept override;
		[[nodiscard]] LightingMutationResult ApplyDeferredLightingParameter(
			const LabParameterId& id) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		bool RefreshInheritedParameters() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		ModelID m_PendingModelId{};
		LoadingProgress m_LoadingProgress{};
		std::unique_ptr<EnvironmentLightingOverrideBase> m_LightingOverride;
	};
}
