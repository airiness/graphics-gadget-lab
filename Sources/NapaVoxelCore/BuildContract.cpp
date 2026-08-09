#include "NapaVoxelCore/BuildContract.h"

#include "NapaVoxelCore/World/VoxelSample.h"

namespace napa::voxel
{
	namespace
	{
		constexpr BuildContract CurrentBuildContract{
			.m_CoreApiVersion = 27,
			.m_VoxelHashSchemaVersion = 1,
			.m_MeshHashSchemaVersion = 1,
			.m_ReferenceMesherVersion = 4,
			.m_EditContractVersion = 3,
			.m_MutationContractVersion = 4,
			.m_DirtyContractVersion = 2,
			.m_RestoreContractVersion = 1,
			.m_IsoValue = IsoValue,
		};
	}

	const BuildContract& GetBuildContract() noexcept
	{
		return CurrentBuildContract;
	}
}
