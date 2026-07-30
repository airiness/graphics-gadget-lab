#include "NapaVoxelCore/BuildContract.h"

#include "NapaVoxelCore/World/VoxelSample.h"

namespace napa::voxel
{
	namespace
	{
		constexpr BuildContract CurrentBuildContract{
			.m_CoreApiVersion = 15,
			.m_VoxelHashSchemaVersion = 1,
			.m_MeshHashSchemaVersion = 1,
			.m_ReferenceMesherVersion = 3,
			.m_IsoValue = IsoValue,
		};
	}

	const BuildContract& GetBuildContract() noexcept
	{
		return CurrentBuildContract;
	}
}
