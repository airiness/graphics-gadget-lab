#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/BuildContract.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstdio>

namespace gglab
{
	void RunNapaVoxelCoreSelfTests(SelfTestContext& context) noexcept
	{
		const napa::voxel::BuildContract& contract =
			napa::voxel::GetBuildContract();

		std::printf(
			"NapaVoxelCore build contract: core-api=%u, voxel-hash-schema=%u, "
			"mesh-hash-schema=%u, reference-mesher=%u, iso=%u\n",
			static_cast<unsigned>(contract.m_CoreApiVersion),
			static_cast<unsigned>(contract.m_VoxelHashSchemaVersion),
			static_cast<unsigned>(contract.m_MeshHashSchemaVersion),
			static_cast<unsigned>(contract.m_ReferenceMesherVersion),
			static_cast<unsigned>(contract.m_IsoValue));

		context.Check(contract.m_CoreApiVersion == 12, "Core API version is 12");
		context.Check(
			contract.m_VoxelHashSchemaVersion == 1,
			"Voxel hash schema version is 1");
		context.Check(
			contract.m_MeshHashSchemaVersion == 1,
			"Mesh hash schema version is 1");
		context.Check(
			contract.m_ReferenceMesherVersion == 3,
			"Reference mesher version is 3");
		context.Check(
			contract.m_IsoValue == napa::voxel::IsoValue,
			"Build contract reports the canonical iso value");

		RunNapaVoxelCoordinateSelfTests(context);
		RunNapaVoxelStorageSelfTests(context);
		RunNapaVoxelHashSelfTests(context);
		RunNapaVoxelRestoreSelfTests(context);
		RunNapaVoxelPrimitiveSelfTests(context);
		RunNapaVoxelMesherSelfTests(context);
	}
}
