#include "Parameters/ParameterSourceAdapters.h"

#include <utility>

namespace gglab
{
	ParameterSourceAdapters::ParameterSourceAdapters(RenderEnvironmentAccess& environment,
		WorldLightingControlBase* worldLighting, HostViewParameterSource::ViewResolver resolver) noexcept :
		m_Domain(environment), m_View(std::move(resolver)), m_Lab(worldLighting, m_Domain), m_Profiles(worldLighting, m_Domain)
	{
	}

	void ParameterSourceAdapters::Bind(DemoBase* demo, LabRuntime* lab, uint64_t identity) noexcept
	{
		m_View.Bind(demo);
		m_Lab.Bind(lab, identity);
		m_Profiles.Bind(demo, identity);
		m_HostParameters = {};
	}

	std::array<ParameterSourceBase*, 3> ParameterSourceAdapters::Ordered() noexcept
	{
		// World mutation commit precedes Lab storage; all callbacks follow the
		// complete commit, including Host values and Session receipt installation.
		return { &m_Profiles, &m_Lab, &m_View };
	}

	void ParameterSourceAdapters::Refresh(bool available) noexcept
	{
		m_View.Refresh();
		m_Profiles.Refresh(available);
		PublishHostParameters();
	}

	void ParameterSourceAdapters::PublishHostParameters() noexcept
	{
		auto host = *m_View.GetParameters();
		for (const auto& parameter : m_Profiles.GetParameters()->GetParameters())
		{
			const bool added = host.Add(parameter.m_Desc);
			GGLAB_ASSERT_MSG(added, "Host parameter source schemas must be distinct.");
			GGLAB_UNUSED(added);
			if (!LabValuesEqual(parameter.m_Value, parameter.m_Desc.m_DefaultValue))
			{
				GGLAB_UNUSED(host.Set(parameter.m_Desc.m_Id, parameter.m_Value));
			}
		}
		m_HostParameters = std::move(host);
	}

	bool ParameterSourceAdapters::ValidateMissing(const LabParameterId& id, std::string& error) noexcept
	{
		return Find(id) || m_Profiles.ValidateMissing(id, error);
	}

	ParameterSourceBase* ParameterSourceAdapters::Find(const LabParameterId& id) noexcept
	{
		ParameterSourceBase* source = nullptr;
		// Composition order is irrelevant to ownership: ambiguity never picks a winner.
		for (auto* candidate : Ordered())
		{
			const auto* parameters = candidate->GetParameters();
			if (parameters && parameters->Find(id))
			{
				if (source)
				{
					return nullptr;
				}
				source = candidate;
			}
		}
		return source;
	}
}
