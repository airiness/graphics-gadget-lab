#pragma once
#include "Application/Lab/LabSessionBase.h"

namespace gglab
{
	class HelloLabSession final : public LabSessionBase
	{
	public:
		explicit HelloLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~HelloLabSession() override = default;

		void Update(float deltaTime) noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	protected:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;

	private:
		bool m_EnableCameraInput = true;
	};
}
