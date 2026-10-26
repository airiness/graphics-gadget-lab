#include "Parameters/ParameterSourceAdapters.h"
#include "Parameters/HostViewParameters.h"
#include "Demo/DemoBase.h"
#include "GGLabRuntime/Graphics/CameraRig.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	class HostViewParameterSource::Prepared final : public PreparedParameterSourceBase
	{
	public:
		Prepared(HostViewParameterSource& owner, std::span<const LabParameterValue> values) noexcept :
			m_Owner(owner), m_Parameters(owner.m_Parameters), m_Values(values.begin(), values.end()),
			m_PreviousOverrides(owner.m_Overrides), m_Overrides(owner.m_Overrides)
		{
			m_Changed.reserve(values.size());
			m_NewOverrides.reserve(values.size());
			m_Overrides.reserve(m_Overrides.size() + values.size());
		}

		bool Prepare(std::string& error) noexcept
		{
			const auto applied = m_Parameters.ApplyBatch(m_Values, error);
			if (!applied)
			{
				return false;
			}
			m_Changed = applied->m_Changed;
			for (const auto& value : m_Values)
			{
				const auto found = std::ranges::find(m_Overrides, value.m_Id, &LabParameterValue::m_Id);
				if (found == m_Overrides.end())
				{
					m_Overrides.push_back(value);
					m_NewOverrides.push_back(value.m_Id);
				}
				else
				{
					*found = value;
				}
			}
			return true;
		}

		void Commit() noexcept override
		{
			m_Owner.m_Parameters = std::move(m_Parameters);
			m_Owner.m_Overrides = std::move(m_Overrides);
		}

		void FinalizeEffects() noexcept override
		{
			const auto resolved = m_Owner.ResolveInherited();
			if (resolved)
			{
				auto settings = resolved->m_View;
				ApplyHostViewParameters(m_PreviousOverrides, settings, resolved->m_Profile.m_EnableScenePreExposure);
				const auto inherited = DescribeHostViewParameters(settings);
				for (const auto& value : m_Values)
				{
					if (!LabValuesEqual(inherited.Find(value.m_Id)->m_Value, value.m_Value) && !IsChanged(value.m_Id))
					{
						// The complete batch's callbacks may have changed an initially equal
						// inherited value. This explicit Host request still takes precedence.
						m_Changed.push_back(value.m_Id);
					}
				}
			}
			for (const auto& id : m_NewOverrides)
			{
				if (!IsChanged(id))
				{
					std::erase_if(m_Owner.m_Overrides, [&](const auto& value) { return value.m_Id == id; });
				}
			}
		}

	private:
		HostViewParameterSource& m_Owner;
		LabParameterSet m_Parameters;
		std::vector<LabParameterValue> m_Values;
		std::vector<LabParameterValue> m_PreviousOverrides;
		std::vector<LabParameterValue> m_Overrides;
		std::vector<LabParameterId> m_NewOverrides;
	};

	// Exact states keep every requested presence: nothing is removed for equality.
	class HostViewParameterSource::PreparedOverrides final : public PreparedParameterSourceBase
	{
	public:
		explicit PreparedOverrides(HostViewParameterSource& owner) noexcept :
			m_Owner(owner), m_Overrides(owner.m_Overrides)
		{
		}

		bool Prepare(std::span<const HostViewOverrideState> states,
			std::vector<LabParameterValue>& targets, std::string& error) noexcept
		{
			const auto inherited = m_Owner.ResolveInherited();
			if (!inherited)
			{
				error = "ParameterPreparationFailed: the display view is unavailable.";
				return false;
			}
			for (const auto& state : states)
			{
				const auto found = std::ranges::find(m_Overrides, state.m_Id, &LabParameterValue::m_Id);
				if (!state.m_Value)
				{
					if (found != m_Overrides.end())
					{
						m_Overrides.erase(found);
					}
				}
				else if (found == m_Overrides.end())
				{
					m_Overrides.push_back({ state.m_Id, *state.m_Value });
				}
				else
				{
					found->m_Value = *state.m_Value;
				}
			}
			// Both sides resolve the same inherited view, so a change is an effective one.
			auto previous = DescribeHostViewParameters(inherited->m_View);
			m_Parameters = previous;
			if ((!m_Owner.m_Overrides.empty() && !previous.ApplyBatch(m_Owner.m_Overrides, error)) ||
				(!m_Overrides.empty() && !m_Parameters.ApplyBatch(m_Overrides, error)))
			{
				error = "ParameterPreparationFailed: " + error;
				return false;
			}
			targets.clear();
			for (const auto& state : states)
			{
				const auto* before = previous.Find(state.m_Id);
				const auto* after = m_Parameters.Find(state.m_Id);
				if (!before || !after)
				{
					error = "ParameterPreparationFailed: the Host view schema changed.";
					return false;
				}
				if (!LabValuesEqual(before->m_Value, after->m_Value))
				{
					m_Changed.push_back(state.m_Id);
				}
				targets.push_back({ state.m_Id, after->m_Value });
			}
			return true;
		}

		void Commit() noexcept override
		{
			m_Owner.m_Parameters = std::move(m_Parameters);
			m_Owner.m_Overrides = std::move(m_Overrides);
		}

	private:
		HostViewParameterSource& m_Owner;
		LabParameterSet m_Parameters;
		std::vector<LabParameterValue> m_Overrides;
	};

	HostViewParameterSource::HostViewParameterSource(ViewResolver resolver) noexcept : m_Resolver(std::move(resolver)) {}

	void HostViewParameterSource::Bind(DemoBase* demo) noexcept
	{
		m_Demo = demo;
		m_Parameters = {};
		m_Overrides.clear();
	}

	std::optional<ApplicationViewSettingsResolution> HostViewParameterSource::ResolveInherited() const noexcept
	{
		if (!m_Demo)
		{
			return std::nullopt;
		}
		if (m_Resolver)
		{
			return m_Resolver(*m_Demo);
		}
		const auto view = m_Demo->GetCameraRig().ResolveEffectiveDisplayView();
		return view.IsValid() ?
			std::optional(ResolveApplicationViewSettings(m_Demo->GetViewRenderProfile(), *view.m_CameraSlot->m_Camera, {})) :
			std::nullopt;
	}

	bool HostViewParameterSource::HasDisplayView() const noexcept
	{
		return ResolveInherited().has_value();
	}

	std::optional<ApplicationViewSettingsResolution> HostViewParameterSource::Resolve() const noexcept
	{
		auto resolution = ResolveInherited();
		if (resolution)
		{
			ApplyViewSettings(resolution->m_View, resolution->m_Profile.m_EnableScenePreExposure);
		}
		return resolution;
	}

	void HostViewParameterSource::Refresh() noexcept
	{
		const auto inherited = ResolveInherited();
		if (!inherited)
		{
			return;
		}
		auto parameters = DescribeHostViewParameters(inherited->m_View);
		if (!m_Overrides.empty())
		{
			std::string error;
			const auto applied = parameters.ApplyBatch(m_Overrides, error);
			GGLAB_ASSERT_MSG(applied.has_value(), "Committed Host overrides must match the fixed Host schema.");
			GGLAB_UNUSED(applied);
		}
		m_Parameters = std::move(parameters);
	}

	std::unique_ptr<PreparedParameterSourceBase> HostViewParameterSource::Prepare(uint64_t id,
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		GGLAB_UNUSED(id);
		auto prepared = std::make_unique<Prepared>(*this, values);
		if (!prepared->Prepare(error))
		{
			return {};
		}
		return prepared;
	}

	std::unique_ptr<PreparedParameterSourceBase> HostViewParameterSource::PrepareOverrides(
		std::span<const HostViewOverrideState> states, std::vector<LabParameterValue>& targets,
		std::string& error) noexcept
	{
		auto prepared = std::make_unique<PreparedOverrides>(*this);
		if (!prepared->Prepare(states, targets, error))
		{
			return {};
		}
		return prepared;
	}

	bool HostViewParameterSource::MatchesFrame(const LabParameterValue& value,
		const ParameterSourceFrame& frame) const noexcept
	{
		const auto effective = ReadHostViewParameter(value.m_Id, frame.m_ViewSettings);
		return effective && LabValuesEqual(*effective, value.m_Value);
	}

	void HostViewParameterSource::ApplyViewSettings(ResolvedViewRenderSettings& settings,
		bool enablePreExposure) const noexcept
	{
		if (!m_Overrides.empty())
		{
			ApplyHostViewParameters(m_Overrides, settings, enablePreExposure);
		}
	}
}
