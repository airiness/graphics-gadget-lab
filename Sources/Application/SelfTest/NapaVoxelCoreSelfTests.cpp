#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"

#include "NapaVoxelCore/BuildContract.h"

#include <cstdio>

namespace gglab
{
	void RunNapaVoxelCoreSelfTests(SelfTestContext& context) noexcept
	{
		const napa::voxel::BuildContract& contract =
			napa::voxel::GetBuildContract();

		std::printf(
			"NapaVoxelCore build contract: core-api=%u, voxel-hash-schema=%u, "
			"mesh-hash-schema=%u, reference-mesher=%u, p0-iso=%u\n",
			static_cast<unsigned>(contract.m_CoreApiVersion),
			static_cast<unsigned>(contract.m_VoxelHashSchemaVersion),
			static_cast<unsigned>(contract.m_MeshHashSchemaVersion),
			static_cast<unsigned>(contract.m_ReferenceMesherVersion),
			static_cast<unsigned>(contract.m_P0IsoValue));

		context.Check(contract.m_CoreApiVersion == 1, "Core API version is 1");
		context.Check(
			contract.m_VoxelHashSchemaVersion == 1,
			"Voxel hash schema version is 1");
		context.Check(
			contract.m_MeshHashSchemaVersion == 1,
			"Mesh hash schema version is 1");
		context.Check(
			contract.m_ReferenceMesherVersion == 1,
			"Reference mesher version is 1");
		context.Check(contract.m_P0IsoValue == 128, "P0 iso value is 128");
	}
}
