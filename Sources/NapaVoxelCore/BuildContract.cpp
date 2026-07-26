#include "NapaVoxelCore/BuildContract.h"

namespace napa::voxel
{
	namespace
	{
		constexpr BuildContract CurrentBuildContract{
			.coreApiVersion = 1,
			.voxelHashSchemaVersion = 1,
			.meshHashSchemaVersion = 1,
			.referenceMesherVersion = 1,
			.p0IsoValue = 128,
		};
	}

	const BuildContract& GetBuildContract() noexcept
	{
		return CurrentBuildContract;
	}
}
