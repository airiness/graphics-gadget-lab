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
		context.Check(!profileWithoutOverride.m_Lighting.m_GTAO.m_Enabled &&
			profileWithoutOverride.m_Lighting.m_GTAO.m_Radius == 0.75f &&
			profileWithOverride.m_Lighting.m_GTAO.m_Enabled &&
			profileWithOverride.m_Lighting.m_GTAO.m_Radius == 2.5f &&
			!activeProfile.m_Lighting.m_GTAO.m_Enabled &&
			activeProfile.m_Lighting.m_GTAO.m_Radius == 0.75f,
			"GTAO DevTools override is explicit and does not mutate the active authoring profile");
	}
}
