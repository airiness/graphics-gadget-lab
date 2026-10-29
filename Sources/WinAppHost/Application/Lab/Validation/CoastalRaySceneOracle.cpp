#include "Application/Lab/Validation/CoastalRaySceneOracle.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <bit>
#include <cmath>
#include <format>

namespace gglab
{
	bool ValidateCoastalRaySceneLayout(const RaySceneDiagnostics& scene, std::string& reason) noexcept
	{
		if (scene.m_GeometryCount != CoastalOracleGeometryCount ||
			scene.m_InstanceCount != CoastalOracleInstanceCount || scene.m_OpaqueInstanceCount != 505 ||
			scene.m_ExcludedBlendCount != 1 || scene.m_MaterialCount != 28 ||
			scene.m_VertexBytes != 18646432 || scene.m_IndexBytes != 2601360)
		{
			reason = "The locked Coastal Ready-model layout changed; review its ray-scene reference.";
			return false;
		}
		return true;
	}

	bool ValidateCoastalRaySceneHits(std::span<const CoastalProbeHit> hits, std::string& reason) noexcept
	{
		if (hits.size() != CoastalProbeRayCount)
		{
			reason = "Coastal probe readback has the wrong ray count.";
			return false;
		}
		for (const auto& expected : CoastalProbeReferences)
		{
			const auto& actual = hits[expected.m_RayIndex];
			if (actual.m_Hit != 1 || actual.m_Errors != 0 || actual.m_Instance != expected.m_Instance ||
				actual.m_Primitive != expected.m_Primitive ||
				!std::isfinite(actual.m_Distance) || !std::isfinite(actual.m_Barycentrics[0]) ||
				!std::isfinite(actual.m_Barycentrics[1]) ||
				std::abs(actual.m_Distance - expected.m_Distance) > CoastalOracleDistanceTolerance ||
				std::abs(actual.m_Barycentrics[0] - expected.m_BarycentricU) > CoastalOracleBarycentricTolerance ||
				std::abs(actual.m_Barycentrics[1] - expected.m_BarycentricV) > CoastalOracleBarycentricTolerance)
			{
				reason = std::format("Coastal reference ray {} differs: hit={}, instance={}, primitive={}, bary=({}, {}), distance={}.",
					expected.m_RayIndex, actual.m_Hit, actual.m_Instance, actual.m_Primitive,
					actual.m_Barycentrics[0], actual.m_Barycentrics[1], actual.m_Distance);
				return false;
			}
		}
		for (uint32_t index = CoastalProbeWidth * CoastalProbeWidth; index < CoastalProbeRayCount; ++index)
		{
			if (hits[index].m_Hit != 0 || hits[index].m_Errors != 0)
			{
				reason = std::format("Coastal reference miss ray {} unexpectedly hit.", index);
				return false;
			}
		}
		return true;
	}

	bool ValidateCoastalRaySceneTables(std::span<const std::byte> geometries,
		std::span<const std::byte> instances, std::string& reason) noexcept
	{
		static_assert(std::endian::native == std::endian::little);
		// Only integer records are hashed. No runtime asset IDs, native handles or floats.
		if (geometries.size() != CoastalOracleGeometryBytes || instances.size() != CoastalOracleInstanceBytes ||
			Sha256DigestToHex(ComputeSha256(geometries)) !=
				"c166a58cb6a38310143d6b926836959484b1c36376baecb1b0e24eec37eac6c4" ||
			Sha256DigestToHex(ComputeSha256(instances)) !=
				"ce20f7ea9935cd7b120e3615e0598a3e77c912a7eb5929840a1b1c504221cd50")
		{
			reason = "The locked Coastal GPU geometry/instance table digest changed.";
			return false;
		}
		return true;
	}
}
