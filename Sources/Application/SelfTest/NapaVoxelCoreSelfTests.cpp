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
			"mesh-hash-schema=%u, reference-mesher=%u, edit=%u, mutation=%u, "
			"dirty=%u, iso=%u\n",
			static_cast<unsigned>(contract.m_CoreApiVersion),
			static_cast<unsigned>(contract.m_VoxelHashSchemaVersion),
			static_cast<unsigned>(contract.m_MeshHashSchemaVersion),
			static_cast<unsigned>(contract.m_ReferenceMesherVersion),
			static_cast<unsigned>(contract.m_EditContractVersion),
			static_cast<unsigned>(contract.m_MutationContractVersion),
			static_cast<unsigned>(contract.m_DirtyContractVersion),
			static_cast<unsigned>(contract.m_IsoValue));

		context.Check(contract.m_CoreApiVersion == 22, "Core API version is 22");
		context.Check(
			contract.m_VoxelHashSchemaVersion == 1,
			"Voxel hash schema version is 1");
		context.Check(
			contract.m_MeshHashSchemaVersion == 1,
			"Mesh hash schema version is 1");
		context.Check(
			contract.m_ReferenceMesherVersion == 3,
			"Reference mesher version is 3");
		context.Check(contract.m_EditContractVersion == 1,
			"Sphere edit contract version is 1");
		context.Check(contract.m_MutationContractVersion == 1,
			"Voxel mutation contract version is 1");
		context.Check(contract.m_DirtyContractVersion == 1,
			"Voxel dirty contract version is 1");
		context.Check(
			contract.m_IsoValue == napa::voxel::IsoValue,
			"Build contract reports the canonical iso value");

		RunNapaVoxelCoordinateSelfTests(context);
		RunNapaVoxelEditSelfTests(context);
		RunNapaVoxelMutationSelfTests(context);
		RunNapaVoxelStorageSelfTests(context);
		RunNapaVoxelHashSelfTests(context);
		RunNapaVoxelRestoreSelfTests(context);
		RunNapaVoxelPrimitiveSelfTests(context);
		RunNapaVoxelMesherSelfTests(context);
		RunNapaVoxelMultiChunkSelfTests(context);
		RunNapaVoxelGGLabAdapterSelfTests(context);
		RunNapaVoxelPublicationSelfTests(context);
	}
}
