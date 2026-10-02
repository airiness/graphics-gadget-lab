#include "Application/SelfTest/DevToolsViewProfileSelfTests.h"

#include "DevTools/DevToolsRuntime.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"

namespace gglab
{
	namespace
	{
		bool ProfilesMatch(const ViewRenderProfile& left, const ViewRenderProfile& right) noexcept
		{
			const auto& leftGTAO = left.m_Lighting.m_GTAO;
			const auto& rightGTAO = right.m_Lighting.m_GTAO;
			const auto& leftBloom = left.m_PostProcess.m_Bloom;
			const auto& rightBloom = right.m_PostProcess.m_Bloom;
			return left.m_TemporalAA == right.m_TemporalAA &&
				left.m_EnableScenePreExposure == right.m_EnableScenePreExposure &&
				left.m_Lighting.m_ForwardPlus.m_Mode == right.m_Lighting.m_ForwardPlus.m_Mode &&
				left.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation ==
					right.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation &&
				left.m_Lighting.m_EnableAerialPerspective == right.m_Lighting.m_EnableAerialPerspective &&
				left.m_Lighting.m_EnableAerialProbe == right.m_Lighting.m_EnableAerialProbe &&
				leftGTAO.m_Enabled == rightGTAO.m_Enabled &&
				leftGTAO.m_Radius == rightGTAO.m_Radius &&
				leftGTAO.m_FalloffStart == rightGTAO.m_FalloffStart &&
				leftGTAO.m_FalloffEnd == rightGTAO.m_FalloffEnd &&
				leftGTAO.m_Thickness == rightGTAO.m_Thickness &&
				leftGTAO.m_Power == rightGTAO.m_Power &&
				leftGTAO.m_DirectionCount == rightGTAO.m_DirectionCount &&
				leftGTAO.m_StepCount == rightGTAO.m_StepCount &&
				leftGTAO.m_DenoiseRadius == rightGTAO.m_DenoiseRadius &&
				leftGTAO.m_FinalAOFormatPreference == rightGTAO.m_FinalAOFormatPreference &&
				leftBloom.m_Enabled == rightBloom.m_Enabled &&
				leftBloom.m_Threshold == rightBloom.m_Threshold &&
				leftBloom.m_SoftKnee == rightBloom.m_SoftKnee &&
				leftBloom.m_Intensity == rightBloom.m_Intensity &&
				leftBloom.m_Scatter == rightBloom.m_Scatter &&
				leftBloom.m_MaxLevels == rightBloom.m_MaxLevels &&
				left.m_PostProcess.m_ToneMapping.m_Operator == right.m_PostProcess.m_ToneMapping.m_Operator;
		}
	}

	void RunDevToolsViewProfileSelfTests(SelfTestContext& context) noexcept
	{
		ViewRenderProfile authoringProfile{};
		authoringProfile.m_Lighting.m_GTAO.m_Enabled = false;
		authoringProfile.m_Lighting.m_GTAO.m_Radius = 0.75f;
		authoringProfile.m_TemporalAA.m_Enabled = false;
		authoringProfile.m_PostProcess.m_Bloom.m_Enabled = false;
		authoringProfile.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation = true;
		authoringProfile.m_Lighting.m_EnableAerialProbe = true;
		const ViewRenderProfile originalAuthoringProfile = authoringProfile;
		DevToolsRuntime devTools;
		auto& overrides = devTools.GetViewRenderSettingsOverrides();
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), originalAuthoringProfile),
			"A new DevTools session inherits every authoring setting, including diagnostics");

		overrides.m_GTAO.m_Settings.m_Radius = 9.0f;
		overrides.m_TemporalAA.m_Settings.m_MaxHistoryFeedback = 0.5f;
		overrides.m_Bloom.m_Settings.m_Intensity = 0.5f;
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), originalAuthoringProfile),
			"Inactive settings blocks neither override nor count as active intent");

		GTAOSettings gtaoSettings = originalAuthoringProfile.m_Lighting.m_GTAO;
		gtaoSettings.m_Enabled = true;
		gtaoSettings.m_Radius = 2.5f;
		gtaoSettings.m_FalloffStart = 0.25f;
		gtaoSettings.m_FalloffEnd = 2.0f;
		gtaoSettings.m_Thickness = 0.5f;
		gtaoSettings.m_Power = 2.0f;
		gtaoSettings.m_DirectionCount = 4;
		gtaoSettings.m_StepCount = 8;
		gtaoSettings.m_DenoiseRadius = 5;
		gtaoSettings.m_FinalAOFormatPreference = GTAOFinalAOFormatPreference::ForceR16Float;
		overrides.m_GTAO.Activate(gtaoSettings);
		ViewRenderProfile expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_GTAO = gtaoSettings;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"GTAO overrides its full block without changing authoring or unrelated features");

		TemporalAASettings temporalSettings = originalAuthoringProfile.m_TemporalAA;
		temporalSettings.m_Enabled = true;
		temporalSettings.m_DepthAbsoluteThreshold = 0.1f;
		temporalSettings.m_DepthRelativeThreshold = 0.04f;
		temporalSettings.m_MaxHistoryFeedback = 0.75f;
		temporalSettings.m_VelocityWeightScale = 0.1f;
		temporalSettings.m_LuminanceWeightScale = 0.25f;
		temporalSettings.m_NeighborhoodClampExpansion = 0.5f;
		overrides.m_TemporalAA.Activate(temporalSettings);
		expectedProfile.m_TemporalAA = temporalSettings;
		context.Check(overrides.GetActiveCount() == 2 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"TAA and GTAO overrides compose while leaving authoring unchanged");

		BloomSettings bloomSettings = originalAuthoringProfile.m_PostProcess.m_Bloom;
		bloomSettings.m_Enabled = true;
		bloomSettings.m_Threshold = 2.0f;
		bloomSettings.m_SoftKnee = 0.25f;
		bloomSettings.m_Intensity = 0.2f;
		bloomSettings.m_Scatter = 0.5f;
		bloomSettings.m_MaxLevels = 4;
		overrides.m_Bloom.Activate(bloomSettings);
		expectedProfile.m_PostProcess.m_Bloom = bloomSettings;
		context.Check(overrides.GetActiveCount() == 3 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"Bloom overrides every tuning parameter while preserving tone mapping and other features");

		overrides.ClearAll();
		overrides.m_ForwardLightingMode = ForwardLightingMode::Legacy;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Lighting-path override preserves independently authored HDR diff intent");

		overrides.ClearAll();
		overrides.m_HdrDiffValidation = false;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation = false;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Explicitly disabling HDR diff counts as an override without changing the lighting path");

		overrides.ClearAll();
		overrides.m_AerialPerspective = false;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_EnableAerialPerspective = false;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Aerial-perspective override preserves independently authored probe intent");

		overrides.ClearAll();
		overrides.m_AerialProbe = false;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_EnableAerialProbe = false;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Aerial-probe override does not change surface transport or other features");

		overrides.ClearAll();
		overrides.m_ScenePreExposure = false;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_EnableScenePreExposure = false;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile),
			"Scene pre-exposure can be overridden off without changing authoring exposure intent");
		overrides.m_ScenePreExposure = originalAuthoringProfile.m_EnableScenePreExposure;
		context.Check(overrides.GetActiveCount() == 1 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), originalAuthoringProfile),
			"An explicit override equal to authoring remains active until reset");

		overrides.m_GTAO.Activate(gtaoSettings);
		overrides.m_TemporalAA.Activate(temporalSettings);
		overrides.m_Bloom.Activate(bloomSettings);
		overrides.m_ForwardLightingMode = ForwardLightingMode::Legacy;
		overrides.m_AerialPerspective = true;
		overrides.m_HdrDiffValidation = false;
		overrides.m_AerialProbe = false;
		expectedProfile = originalAuthoringProfile;
		expectedProfile.m_Lighting.m_GTAO = gtaoSettings;
		expectedProfile.m_TemporalAA = temporalSettings;
		expectedProfile.m_PostProcess.m_Bloom = bloomSettings;
		expectedProfile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		expectedProfile.m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation = false;
		expectedProfile.m_Lighting.m_EnableAerialProbe = false;
		context.Check(overrides.GetActiveCount() == 8 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(authoringProfile), expectedProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"All eight overrides compose, counting blocks once and explicit false values as active");

		// A different authoring source must not silently clear session overrides or
		// re-seed the parameters of an already active settings block.
		ViewRenderProfile nextAuthoringProfile = originalAuthoringProfile;
		nextAuthoringProfile.m_Lighting.m_GTAO.m_Radius = 7.0f;
		nextAuthoringProfile.m_TemporalAA.m_MaxHistoryFeedback = 0.9f;
		nextAuthoringProfile.m_PostProcess.m_Bloom.m_Intensity = 0.4f;
		nextAuthoringProfile.m_Lighting.m_EnableAerialPerspective = false;
		nextAuthoringProfile.m_EnableScenePreExposure = false;
		const ViewRenderProfile originalNextAuthoringProfile = nextAuthoringProfile;
		context.Check(overrides.GetActiveCount() == 8 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile) &&
			ProfilesMatch(nextAuthoringProfile, originalNextAuthoringProfile),
			"DevTools session overrides survive authoring-profile switches without mutating either source");

		overrides.m_GTAO.Reset();
		expectedProfile.m_Lighting.m_GTAO = nextAuthoringProfile.m_Lighting.m_GTAO;
		context.Check(overrides.GetActiveCount() == 7 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile),
			"Resetting one block restores the current authoring source and preserves sibling overrides");
		overrides.m_AerialPerspective.reset();
		expectedProfile.m_Lighting.m_EnableAerialPerspective = false;
		context.Check(overrides.GetActiveCount() == 6 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), expectedProfile),
			"Resetting one scalar restores inheritance without clearing diagnostic or other overrides");

		overrides.ClearAll();
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), originalNextAuthoringProfile),
			"Clear All restores every current authoring setting, including diagnostic intent");
		nextAuthoringProfile.m_Lighting.m_GTAO.m_Radius = 3.0f;
		nextAuthoringProfile.m_PostProcess.m_Bloom.m_Intensity = 0.6f;
		nextAuthoringProfile.m_Lighting.m_EnableAerialPerspective = true;
		context.Check(ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), nextAuthoringProfile),
			"Cleared overrides keep following subsequent authoring edits rather than a captured profile");

		overrides.m_Bloom.Activate(bloomSettings);
		overrides.m_ScenePreExposure = false;
		overrides.m_HdrDiffValidation = false;
		overrides.m_AerialProbe = false;
		DevToolsRuntime nextDevToolsSession;
		context.Check(overrides.GetActiveCount() == 4 &&
			nextDevToolsSession.GetViewRenderSettingsOverrides().GetActiveCount() == 0 &&
			ProfilesMatch(nextDevToolsSession.ResolveViewRenderProfile(nextAuthoringProfile), nextAuthoringProfile),
			"Session overrides are instance-owned and never inherited by a new DevTools session");
		devTools.Reset();
		context.Check(overrides.GetActiveCount() == 0 &&
			ProfilesMatch(devTools.ResolveViewRenderProfile(nextAuthoringProfile), nextAuthoringProfile) &&
			ProfilesMatch(authoringProfile, originalAuthoringProfile),
			"DevTools reset clears rendering and diagnostic overrides without modifying authoring state");
	}
}
