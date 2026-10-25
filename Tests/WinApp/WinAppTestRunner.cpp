#include "WinAppTestRunner.h"
#include "ApplicationHostConfigurationSelfTests.h"
#include "ApplicationContentRegistrationSelfTests.h"
#include "ApplicationControlSelfTests.h"
#include "ApplicationInputSelfTests.h"
#include "ApplicationLifecycleSelfTests.h"
#include "DevToolsViewProfileSelfTests.h"
#include "TemporalAALabDiagnosticsSelfTests.h"
#include "TemporalAALabGpuTimingSelfTests.h"
#include "FrameCaptureImageEncoderSelfTests.h"
#include "ApplicationFrameCaptureSelfTests.h"
#include "LaunchOptionsSelfTests.h"
#include "NapaVoxelSelfTests.h"
#include "RuntimePathSelfTests.h"
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
#include "DevelopmentShaderBuildProcessClientSelfTests.h"
#include "DevelopmentShaderHotReloadSelfTests.h"
#endif
#if GGLAB_ENABLE_VULKAN
#include "DevelopGuiVulkanPresentationContractSelfTests.h"
#endif
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "RuntimePaths.h"

#include <algorithm>
#include <array>
#include <filesystem>

namespace gglab
{
	namespace
	{
		std::filesystem::path g_ApplicationSelfTestAssetRoot;

		constexpr std::array RegisteredSuites
		{
			SelfTestSuiteDesc{ .m_Id = "app-content-registration", .m_Run = &RunApplicationContentRegistrationSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-input", .m_Run = &RunApplicationInputSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-host-configuration", .m_Run = &RunApplicationHostConfigurationSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-lifecycle", .m_Run = &RunApplicationLifecycleSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-devtools-view-profile", .m_Run = &RunDevToolsViewProfileSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-control", .m_Run = &RunApplicationControlSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-temporal-aa-lab-diagnostics", .m_Run = &RunTemporalAALabDiagnosticsSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-temporal-aa-lab-gpu-timing", .m_Run = &RunTemporalAALabGpuTimingSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-frame-capture-encoder", .m_Run = &RunFrameCaptureImageEncoderSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-frame-capture", .m_Run = &RunApplicationFrameCaptureSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-launch-options", .m_Run = &RunLaunchOptionsSelfTests, },
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
			SelfTestSuiteDesc{ .m_Id = "app-development-shader-process-client", .m_Run = &RunDevelopmentShaderBuildProcessClientSelfTests, },
			SelfTestSuiteDesc{ .m_Id = "app-development-shader-hot-reload", .m_Run = &RunDevelopmentShaderHotReloadSelfTests, },
#endif
			SelfTestSuiteDesc{ .m_Id = "napa-voxel", .m_Run = &RunNapaVoxelSelfTests, },
#if GGLAB_ENABLE_VULKAN
			SelfTestSuiteDesc{ .m_Id = "app-developgui-vulkan-presentation-contract", .m_Run = &RunDevelopGuiVulkanPresentationContractSelfTests, },
#endif
		};

		[[nodiscard]] const SelfTestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
		{
			const auto iterator = std::ranges::find(RegisteredSuites, suiteId, &SelfTestSuiteDesc::m_Id);
			return iterator != RegisteredSuites.end() ? &*iterator : nullptr;
		}
	}

	bool IsApplicationSelfTestSelectionValid(std::string_view selection) noexcept
	{
		return selection == AllApplicationSelfTestsSelection ||
			selection == ApplicationPathCompositionSelfTestSelection ||
			selection == ApplicationArtifactPackageClosureSelfTestSelection ||
			FindSuite(selection) != nullptr;
	}

	const std::filesystem::path& GetApplicationSelfTestAssetRoot() noexcept
	{
		return g_ApplicationSelfTestAssetRoot;
	}

	bool RunApplicationSelfTests(std::string_view selection, const RuntimePaths& runtimePaths) noexcept
	{
		g_ApplicationSelfTestAssetRoot = runtimePaths.m_AssetRoot;
		const bool runAll = selection == AllApplicationSelfTestsSelection;
		const SelfTestSuiteDesc* suite = runAll ? nullptr : FindSuite(selection);
		if (!runAll && !suite)
		{
			return false;
		}

		InitializeLogging();
		ConsoleSelfTestReporter reporter;
		if (runAll)
		{
			bool allSucceeded = true;
			for (const SelfTestSuiteDesc& registeredSuite : RegisteredSuites)
			{
				if (!RunSelfTestSuite(registeredSuite, reporter))
				{
					allSucceeded = false;
				}
			}
			return allSucceeded;
		}
		return RunSelfTestSuite(*suite, reporter);
	}

	bool RunApplicationPathCompositionSelfTest(const std::filesystem::path& application) noexcept
	{
		InitializeLogging();
		ConsoleSelfTestReporter reporter;
		reporter.OnSuiteStarted(ApplicationPathCompositionSelfTestSelection);
		SelfTestContext context(reporter);
		RunApplicationPathCompositionSelfTests(context, application);
		reporter.OnSuiteFinished(ApplicationPathCompositionSelfTestSelection, context.GetSummary());
		return context.GetSummary().Succeeded();
	}

	bool RunApplicationArtifactPackageClosureSelfTest(const std::filesystem::path& application) noexcept
	{
		InitializeLogging();
		ConsoleSelfTestReporter reporter;
		reporter.OnSuiteStarted(ApplicationArtifactPackageClosureSelfTestSelection);
		SelfTestContext context(reporter);
		RunApplicationArtifactPackageClosureSelfTests(context, application);
		reporter.OnSuiteFinished(ApplicationArtifactPackageClosureSelfTestSelection, context.GetSummary());
		return context.GetSummary().Succeeded();
	}
}
