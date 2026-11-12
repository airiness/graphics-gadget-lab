#include "ArtifactCacheSelfTests.h"
#include "AssetDataSelfTests.h"
#include "AssetPublicationSelfTests.h"
#include "AssetResidencySelfTests.h"
#include "AssetUploadSchedulerSelfTests.h"
#include "CameraPathSelfTests.h"
#include "DiagnosticsContractSelfTests.h"
#include "EnvironmentSelectionSelfTests.h"
#include "FrameCaptureSelfTests.h"
#include "LayoutContractSelfTests.h"
#include "NonFiniteWatchSelfTests.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "PublicationAccountingSelfTests.h"
#include "RayQueryContractSelfTests.h"
#include "RenderingContractSelfTests.h"
#include "RGObjectArenaSelfTests.h"
#include "RenderServicesViewSelfTests.h"
#include "ShaderArtifactRuntimeSelfTests.h"
#if GGLAB_ENABLE_VULKAN
#include "VulkanContractSelfTests.h"
#endif

#include <algorithm>
#include <string_view>

namespace
{
	constexpr gglab::SelfTestSuiteDesc RegisteredSuites[]{
		gglab::SelfTestSuiteDesc{
			.m_Id = "artifact-cache",
			.m_Run = &gglab::RunArtifactCacheSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "asset-data",
			.m_Run = &gglab::RunAssetDataSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "asset-publication",
			.m_Run = &gglab::RunAssetPublicationSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "asset-residency",
			.m_Run = &gglab::RunAssetResidencySelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "asset-upload-scheduler",
			.m_Run = &gglab::RunAssetUploadSchedulerSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "camera-paths",
			.m_Run = &gglab::RunCameraPathSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "diagnostics-contracts",
			.m_Run = &gglab::RunDiagnosticsContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "environment-selection",
			.m_Run = &gglab::RunEnvironmentSelectionSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "frame-capture",
			.m_Run = &gglab::RunFrameCaptureSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "nonfinite-watch",
			.m_Run = &gglab::RunNonFiniteWatchSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "layout-contracts",
			.m_Run = &gglab::RunLayoutContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "publication-accounting",
			.m_Run = &gglab::RunPublicationAccountingSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "shader-artifact-runtime",
			.m_Run = &gglab::RunShaderArtifactRuntimeSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "ray-query-contracts",
			.m_Run = &gglab::RunRayQueryContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "rendering-contracts",
			.m_Run = &gglab::RunRenderingContractSelfTests,
		},
#if GGLAB_ENABLE_VULKAN
		gglab::SelfTestSuiteDesc{
			.m_Id = "vulkan-contracts",
			.m_Run = &gglab::RunVulkanContractSelfTests,
		},
#endif
	};

	// Keep focused selections out of all to avoid repeating aggregate coverage.
	constexpr gglab::SelfTestSuiteDesc FocusedSuites[]
	{
		gglab::SelfTestSuiteDesc{ "rendering-bindings", &gglab::RunRenderingBindingsSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-depth", &gglab::RunRenderingDepthSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-graph", &gglab::RunRenderingGraphSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-lighting", &gglab::RunRenderingLightingSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-materials", &gglab::RunRenderingMaterialsSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-previews", &gglab::RunRenderingPreviewsSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-resources", &gglab::RunRenderingResourcesSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-scene", &gglab::RunRenderingSceneSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-services", &gglab::RunRenderingServicesSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-shadows", &gglab::RunRenderingShadowsSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-submission", &gglab::RunRenderingSubmissionSelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-temporal-aa", &gglab::RunRenderingTemporalAASelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-temporal-history", &gglab::RunRenderingTemporalHistorySelfTests },
		gglab::SelfTestSuiteDesc{ "rendering-uploads", &gglab::RunRenderingUploadsSelfTests },
	};

	[[nodiscard]] const gglab::SelfTestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
	{
		const auto iterator = std::ranges::find(RegisteredSuites, suiteId, &gglab::SelfTestSuiteDesc::m_Id);
		if (iterator != std::ranges::end(RegisteredSuites))
		{
			return &*iterator;
		}
		const auto focused = std::ranges::find(FocusedSuites, suiteId, &gglab::SelfTestSuiteDesc::m_Id);
		return focused != std::ranges::end(FocusedSuites) ? &*focused : nullptr;
	}

	[[nodiscard]] bool RunSelection(std::string_view selection) noexcept
	{
		const bool runAll = selection == "all";
		const gglab::SelfTestSuiteDesc* suite = runAll ? nullptr : FindSuite(selection);
		if (!runAll && !suite)
		{
			return false;
		}

		gglab::InitializeLogging();
		gglab::ConsoleSelfTestReporter reporter;
		if (runAll)
		{
			bool allSucceeded = true;
			for (const gglab::SelfTestSuiteDesc& registeredSuite : RegisteredSuites)
			{
				if (!gglab::RunSelfTestSuite(registeredSuite, reporter))
				{
					allSucceeded = false;
				}
			}
			return allSucceeded;
		}
		return gglab::RunSelfTestSuite(*suite, reporter);
	}
}

int main(int argumentCount, char* arguments[])
{
	if (argumentCount == 3 && std::string_view(arguments[1]) == "--render-services-child")
	{
		return gglab::RunRenderServicesViewChild(arguments[2]);
	}
	if (argumentCount == 2 && std::string_view(arguments[1]) == "--rg-object-storage-benchmark")
	{
		gglab::RunRenderGraphObjectStorageBenchmark();
		return 0;
	}
	if (argumentCount == 3 && std::string_view(arguments[1]) == "--rg-object-arena-child")
	{
		return gglab::RunRGObjectArenaChild(arguments[2]);
	}
	std::string_view selection = "all";
	for (int index = 1; index < argumentCount; ++index)
	{
		const std::string_view argument = arguments[index];
		if (argument == "--suite")
		{
			if (index + 1 >= argumentCount)
			{
				return 2;
			}
			selection = arguments[++index];
		}
		else
		{
			return 2;
		}
	}
	return RunSelection(selection) ? 0 : 1;
}
