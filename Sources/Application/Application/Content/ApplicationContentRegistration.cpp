#include "Application/Content/ApplicationContentRegistration.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace gglab
{
	bool ApplicationContentRegistration::IsValid() const noexcept
	{
		if (m_Demos.empty())
		{
			return false;
		}

		uint32_t labRuntimeProviderCount = 0;
		for (size_t index = 0; index < m_Demos.size(); ++index)
		{
			const ApplicationDemoRegistration& registration = m_Demos[index];
			if (registration.m_Id.empty() || !registration.m_Factory)
			{
				return false;
			}
			if (registration.m_ProvidesLabRuntime)
			{
				++labRuntimeProviderCount;
			}
			for (size_t priorIndex = 0; priorIndex < index; ++priorIndex)
			{
				if (m_Demos[priorIndex].m_Id == registration.m_Id)
				{
					return false;
				}
			}
		}
		if (labRuntimeProviderCount > 1 || (labRuntimeProviderCount == 1 && m_Labs.empty()))
		{
			return false;
		}

		LabCatalog catalog;
		for (const LabRegistration& registration : m_Labs)
		{
			if (!catalog.Register(registration.m_Descriptor, registration.m_Factory))
			{
				return false;
			}
		}
		return true;
	}

	const ApplicationDemoRegistration* ApplicationContentRegistration::FindDemo(
		std::string_view id) const noexcept
	{
		const auto iterator = std::ranges::find(m_Demos, id, &ApplicationDemoRegistration::m_Id);
		return iterator != m_Demos.end() ? &*iterator : nullptr;
	}

	bool ApplicationContentRegistration::ContainsLab(const LabId& id) const noexcept
	{
		return std::ranges::any_of(m_Labs,
			[&id](const LabRegistration& registration) noexcept
			{ return registration.m_Descriptor.m_Id == id; });
	}

	ApplicationContentSelection ResolveApplicationContentSelection(
		const ApplicationContentRegistration& registration, std::string_view startupDemoId,
		std::optional<std::string_view> startupLabId) noexcept
	{
		if (!registration.IsValid())
		{
			return { .m_Status = ApplicationContentSelectionStatus::InvalidRegistration };
		}

		const ApplicationDemoRegistration* startupDemo = registration.FindDemo(startupDemoId);
		if (!startupDemo)
		{
			return { .m_Status = ApplicationContentSelectionStatus::StartupDemoUnavailable };
		}

		LabId startupLab;
		if (startupLabId)
		{
			startupLab = LabId(*startupLabId);
			if (!registration.ContainsLab(startupLab))
			{
				return { .m_Status = ApplicationContentSelectionStatus::StartupLabUnavailable };
			}
		}
		if (startupDemo->m_ProvidesLabRuntime && !startupLab.IsValid())
		{
			return { .m_Status = ApplicationContentSelectionStatus::StartupLabUnavailable };
		}

		return {
			.m_Status = ApplicationContentSelectionStatus::Succeeded,
			.m_StartupDemo = startupDemo,
			.m_StartupLab = std::move(startupLab),
		};
	}
}
