#pragma once

#include <cstdint>

namespace napa::voxel
{
	struct BuildContract
	{
		std::uint32_t coreApiVersion;
		std::uint32_t voxelHashSchemaVersion;
		std::uint32_t meshHashSchemaVersion;
		std::uint32_t referenceMesherVersion;
		std::uint8_t p0IsoValue;
	};

	[[nodiscard]] const BuildContract& GetBuildContract() noexcept;
}
