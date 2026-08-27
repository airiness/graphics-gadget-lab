#include "Application/SelfTest/DevToolsViewProfileSelfTests.h"

#include "DevTools/DevToolsRuntime.h"
#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"

namespace gglab
{
	void RunDevToolsViewProfileSelfTests(SelfTestContext& context) noexcept
	{
		ViewRenderProfile activeProfile{};
		activeProfile.m_Lighting.m_GTAO.m_Enabled = false;
		activeProfile.m_Lighting.m_GTAO.m_Radius = 0.75f;
		activeProfile.m_TemporalAA.m_Enabled = false;
		DevToolsRuntime devTools;
		const ViewRenderProfile profileWithoutOverride =
			devTools.ResolveViewRenderProfile(activeProfile);
		auto& gtaoOverride = devTools.GetViewRenderSettingsOverrides().m_GTAO;
		gtaoOverride.m_Settings = activeProfile.m_Lighting.m_GTAO;
		gtaoOverride.m_Settings.m_Enabled = true;
		gtaoOverride.m_Settings.m_Radius = 2.5f;
		gtaoOverride.m_IsActive = true;
		const ViewRenderProfile profileWithOverride =
			devTools.ResolveViewRenderProfile(activeProfile);
		auto& temporalOverride = devTools.GetViewRenderSettingsOverrides().m_TemporalAA;
		temporalOverride.m_Settings = activeProfile.m_TemporalAA;
		temporalOverride.m_Settings.m_Enabled = true;
		temporalOverride.m_Settings.m_HistoryWeight = 0.75f;
		temporalOverride.m_IsActive = true;
		const ViewRenderProfile profileWithBothOverrides =
			devTools.ResolveViewRenderProfile(activeProfile);
		context.Check(!profileWithoutOverride.m_Lighting.m_GTAO.m_Enabled &&
			profileWithoutOverride.m_Lighting.m_GTAO.m_Radius == 0.75f &&
			profileWithOverride.m_Lighting.m_GTAO.m_Enabled &&
			profileWithOverride.m_Lighting.m_GTAO.m_Radius == 2.5f &&
			!activeProfile.m_Lighting.m_GTAO.m_Enabled &&
			activeProfile.m_Lighting.m_GTAO.m_Radius == 0.75f,
			"GTAO DevTools override is explicit and does not mutate the active authoring profile");
		context.Check(!profileWithOverride.m_TemporalAA.m_Enabled &&
			profileWithBothOverrides.m_TemporalAA.m_Enabled &&
			profileWithBothOverrides.m_TemporalAA.m_HistoryWeight == 0.75f &&
			!activeProfile.m_TemporalAA.m_Enabled,
			"Temporal AA DevTools override is explicit and does not mutate authoring state");
	}
}
