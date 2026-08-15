#include "NapaVoxelCoreTestCases.h"
#include "NapaVoxelDataOnlyPublicationSelfTests.h"
#include "NapaVoxelTestFramework.h"

#include "NapaVoxelCore/BuildContract.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace
{
	using namespace napa::voxel::testing;

	void RunBuildContractTests(TestContext& context) noexcept
	{
		const napa::voxel::BuildContract& contract =
			napa::voxel::GetBuildContract();

		std::printf(
			"NapaVoxelCore build contract: core-api=%u, voxel-hash-schema=%u, "
			"mesh-hash-schema=%u, reference-mesher=%u, edit=%u, mutation=%u, "
			"dirty=%u, restore=%u, iso=%u\n",
			static_cast<unsigned>(contract.m_CoreApiVersion),
			static_cast<unsigned>(contract.m_VoxelHashSchemaVersion),
			static_cast<unsigned>(contract.m_MeshHashSchemaVersion),
			static_cast<unsigned>(contract.m_ReferenceMesherVersion),
			static_cast<unsigned>(contract.m_EditContractVersion),
			static_cast<unsigned>(contract.m_MutationContractVersion),
			static_cast<unsigned>(contract.m_DirtyContractVersion),
			static_cast<unsigned>(contract.m_RestoreContractVersion),
			static_cast<unsigned>(contract.m_IsoValue));

		context.Check(contract.m_CoreApiVersion == 29, "Core API version is 29");
		context.Check(
			contract.m_VoxelHashSchemaVersion == 1,
			"Voxel hash schema version is 1");
		context.Check(
			contract.m_MeshHashSchemaVersion == 1,
			"Mesh hash schema version is 1");
		context.Check(
			contract.m_ReferenceMesherVersion == 6,
			"Reference mesher version is 6");
		context.Check(contract.m_EditContractVersion == 3,
			"Sphere edit contract version is 3");
		context.Check(contract.m_MutationContractVersion == 4,
			"Voxel mutation contract version is 4");
		context.Check(contract.m_DirtyContractVersion == 2,
			"Gradient-aware voxel dirty contract version is 2");
		context.Check(contract.m_RestoreContractVersion == 1,
			"Voxel Restore contract version is 1");
		context.Check(
			contract.m_IsoValue == napa::voxel::IsoValue,
			"Build contract reports the canonical iso value");
	}

	constexpr std::array RegisteredSuites{
		TestSuiteDesc{
			.m_Id = "build-contract",
			.m_Run = &RunBuildContractTests,
		},
		TestSuiteDesc{
			.m_Id = "coordinate",
			.m_Run = &RunNapaVoxelCoordinateSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "damage",
			.m_Run = &RunNapaVoxelDamageSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "edit",
			.m_Run = &RunNapaVoxelEditSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "hash",
			.m_Run = &RunNapaVoxelHashSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "mesher",
			.m_Run = &RunNapaVoxelMesherSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "multi-chunk",
			.m_Run = &RunNapaVoxelMultiChunkSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "mutation",
			.m_Run = &RunNapaVoxelMutationSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "primitive",
			.m_Run = &RunNapaVoxelPrimitiveSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "restore",
			.m_Run = &RunNapaVoxelRestoreSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "storage",
			.m_Run = &RunNapaVoxelStorageSelfTests,
		},
		TestSuiteDesc{
			.m_Id = "publication-data-only",
			.m_Run = &RunNapaVoxelDataOnlyPublicationSelfTests,
		},
	};

	[[nodiscard]] const TestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
	{
		const auto iterator =
			std::ranges::find(RegisteredSuites, suiteId, &TestSuiteDesc::m_Id);
		return iterator != RegisteredSuites.end() ? &*iterator : nullptr;
	}

	[[nodiscard]] bool RunSelection(std::string_view selection) noexcept
	{
		const bool runAll = selection == "all";
		const TestSuiteDesc* suite = runAll ? nullptr : FindSuite(selection);
		if (!runAll && !suite)
		{
			return false;
		}

		ConsoleTestReporter reporter;
		if (runAll)
		{
			bool allSucceeded = true;
			for (const TestSuiteDesc& registeredSuite : RegisteredSuites)
			{
				if (!RunTestSuite(registeredSuite, reporter))
				{
					allSucceeded = false;
				}
			}
			return allSucceeded;
		}
		return RunTestSuite(*suite, reporter);
	}
}

int main(int argumentCount, char* arguments[])
{
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
