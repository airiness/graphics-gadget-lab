#include "NapaVoxelCore/Hash/VoxelWorldHash.h"

#include "NapaVoxelCore/BuildContract.h"
#include "NapaVoxelCore/Hash/CanonicalHash.h"
#include "Hash/CanonicalVoxelSerialization.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <cstdint>

namespace napa::voxel
{
	ValidationResult ComputeLogicalVoxelWorldHash(
		const VoxelWorld& world, std::uint64_t& hash) noexcept
	{
		const VoxelWorldConfig& config = world.GetConfig();
		const SampleAabb sampleBounds = world.GetLogicalSampleBounds();

		CanonicalHashWriter writer;
		writer.WriteU32(GetBuildContract().m_VoxelHashSchemaVersion);
		writer.WriteU32(VoxelDataSchemaVersion);
		WriteCanonicalVoxelWorldConfig(writer, config);

		for (std::int64_t z = sampleBounds.m_Min.m_Z; z < sampleBounds.m_MaxExclusive.m_Z; ++z)
		{
			for (std::int64_t y = sampleBounds.m_Min.m_Y; y < sampleBounds.m_MaxExclusive.m_Y; ++y)
			{
				for (std::int64_t x = sampleBounds.m_Min.m_X; x < sampleBounds.m_MaxExclusive.m_X;
					++x)
				{
					VoxelSample sample{};
					const ValidationResult readResult = world.ReadCurrentSample(
						{
							static_cast<std::int32_t>(x),
							static_cast<std::int32_t>(y),
							static_cast<std::int32_t>(z),
						},
						sample);
					if (readResult.Failed())
					{
						return readResult;
					}

					const ValidationResult sampleResult = ValidateVoxelSample(sample);
					if (sampleResult.Failed())
					{
						return sampleResult;
					}

					writer.WriteU8(sample.m_Density);
					writer.WriteEnum(sample.m_Material);
					writer.WriteU8(sample.m_Damage);
				}
			}
		}

		hash = writer.GetValue();
		return {};
	}
}
