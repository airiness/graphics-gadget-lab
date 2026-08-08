#pragma once

#include <cstdint>

namespace napa::voxel
{
	struct BuildContract
	{
		std::uint32_t m_CoreApiVersion = 0;
		std::uint32_t m_VoxelHashSchemaVersion = 0;
		std::uint32_t m_MeshHashSchemaVersion = 0;
		std::uint32_t m_ReferenceMesherVersion = 0;
		std::uint32_t m_EditContractVersion = 0;
		std::uint32_t m_MutationContractVersion = 0;
		std::uint32_t m_DirtyContractVersion = 0;
		std::uint8_t m_IsoValue = 0;
	};

	[[nodiscard]] const BuildContract& GetBuildContract() noexcept;
}
