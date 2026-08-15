#pragma once

#include <cstdint>

namespace napa::voxel::detail
{
	extern thread_local std::uint64_t SimulatedAuthoritativeRevision;
	extern thread_local bool SimulatePublicationAllocationFailure;
}
