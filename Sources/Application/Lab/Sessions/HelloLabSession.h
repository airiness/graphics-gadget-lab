#pragma once
#include "Application/Lab/LabSession.h"

namespace gglab
{
	class HelloLabSession final : public LabSession
	{
	public:
		explicit HelloLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~HelloLabSession() override = default;

		void Update() noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSession> Create(
			const LabSessionCreateInfo& createInfo) noexcept;
	};
}
