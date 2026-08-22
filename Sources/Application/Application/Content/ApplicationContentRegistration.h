#pragma once
#include "Application/Demo/DemoBase.h"
#include "Application/Lab/LabCatalog.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	using ApplicationDemoFactory = std::unique_ptr<DemoBase> (*)(
		const DemoCreateInfo& createInfo, const LabId& startupLab,
		std::span<const LabRegistration> labRegistrations) noexcept;

	struct ApplicationDemoRegistration
	{
		std::string m_Id;
		ApplicationDemoFactory m_Factory = nullptr;
		bool m_ProvidesLabRuntime = false;
	};

	struct ApplicationContentRegistration
	{
		std::vector<ApplicationDemoRegistration> m_Demos;
		std::vector<LabRegistration> m_Labs;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] const ApplicationDemoRegistration* FindDemo(
			std::string_view id) const noexcept;
		[[nodiscard]] bool ContainsLab(const LabId& id) const noexcept;
	};

	enum class ApplicationContentSelectionStatus : uint8_t
	{
		Succeeded,
		InvalidRegistration,
		StartupDemoUnavailable,
		StartupLabUnavailable,
	};

	struct ApplicationContentSelection
	{
		ApplicationContentSelectionStatus m_Status =
			ApplicationContentSelectionStatus::InvalidRegistration;
		const ApplicationDemoRegistration* m_StartupDemo = nullptr;
		LabId m_StartupLab;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_Status == ApplicationContentSelectionStatus::Succeeded;
		}
	};

	[[nodiscard]] ApplicationContentSelection ResolveApplicationContentSelection(
		const ApplicationContentRegistration& registration, std::string_view startupDemoId,
		std::optional<std::string_view> startupLabId) noexcept;
}
