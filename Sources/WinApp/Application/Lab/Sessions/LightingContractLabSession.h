#pragma once
#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <memory>

namespace gglab
{
	class LightingContractLabSession final : public LabSessionBase
	{
	public:
		explicit LightingContractLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~LightingContractLabSession() override = default;

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

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		ModelID m_PendingModelId{};
		LoadingProgress m_LoadingProgress{};
		float m_PreviousEnvironmentIntensity = 1.0f;
		bool m_PreviousSkyboxEnabled = true;
		bool m_HasEnvironmentOverride = false;
	};
}
