#include "WinAppTestRunner.h"
#include "Application/ApplicationHostConfiguration.h"
#include "Application/Platform/Windows/Win32UnattendedProcess.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "RuntimePaths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>

int main(int argumentCount, char** arguments)
{
	gglab::win32::ConfigureUnattendedFailureReporting();
	std::string_view selection;
	std::filesystem::path application;
	std::filesystem::path assetRoot;
	bool suiteSpecified = false;
	bool applicationSpecified = false;
	bool assetRootSpecified = false;
	for (int index = 1; index < argumentCount; ++index)
	{
		const std::string_view argument(arguments[index]);
		if (argument == "--help")
		{
			std::puts("Usage: WinAppTests.exe --suite <id|all> [--asset-root <path>] [--application <exe>]\n"
				"Structural suites require the actual product executable via --application.");
			return EXIT_SUCCESS;
		}
		if (index + 1 >= argumentCount)
		{
			std::fputs("Error: A test option requires a value.\n", stderr);
			return 2;
		}
		if (std::string_view(arguments[index + 1]).empty())
		{
			std::fputs("Error: Test option values must not be empty.\n", stderr);
			return 2;
		}
		if (argument == "--suite" && !suiteSpecified)
		{
			suiteSpecified = true;
			selection = arguments[++index];
		}
		else if (argument == "--application" && !applicationSpecified)
		{
			applicationSpecified = true;
			application = arguments[++index];
		}
		else if (argument == "--asset-root" && !assetRootSpecified)
		{
			assetRootSpecified = true;
			assetRoot = arguments[++index];
		}
		else
		{
			std::fputs("Error: Unknown or duplicate test option.\n", stderr);
			return 2;
		}
	}
	if (!gglab::IsApplicationSelfTestSelectionValid(selection) ||
		(!application.empty() && !application.is_absolute()) ||
		(!assetRoot.empty() && !assetRoot.is_absolute()))
	{
		std::fputs("Error: Select a known suite and use absolute input paths.\n", stderr);
		return 2;
	}
	if (selection == gglab::ApplicationPathCompositionSelfTestSelection ||
		selection == gglab::ApplicationArtifactPackageClosureSelfTestSelection)
	{
		if (assetRootSpecified)
		{
			std::fputs("Error: Structural suites use the product's asset paths.\n", stderr);
			return 2;
		}
		if (application.empty())
		{
			std::fputs("Error: Structural suites require --application <absolute-exe>.\n", stderr);
			return 2;
		}
		const bool passed = selection == gglab::ApplicationPathCompositionSelfTestSelection ?
			gglab::RunApplicationPathCompositionSelfTest(application) :
			gglab::RunApplicationArtifactPackageClosureSelfTest(application);
		return passed ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (!application.empty())
	{
		std::fputs("Error: --application belongs to the structural suites.\n", stderr);
		return 2;
	}
	auto paths = gglab::BuildRuntimePaths(gglab::win32::GetExecutableDirectory());
	if (!assetRoot.empty())
	{
		paths.m_AssetRoot = assetRoot;
	}
	return gglab::RunApplicationSelfTests(selection, paths) ? EXIT_SUCCESS : EXIT_FAILURE;
}
