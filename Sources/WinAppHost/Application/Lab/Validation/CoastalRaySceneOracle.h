#pragma once
#include "GGLabRuntime/Graphics/RayScene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	inline constexpr uint32_t CoastalProbeWidth = 32;
	inline constexpr uint32_t CoastalProbeRayCount = CoastalProbeWidth * CoastalProbeWidth + 2;
	inline constexpr uint32_t CoastalOracleGeometryCount = 59;
	inline constexpr uint32_t CoastalOracleInstanceCount = 506;
	inline constexpr uint64_t CoastalOracleGeometryBytes = CoastalOracleGeometryCount * sizeof(RaySceneGeometryGPU);
	inline constexpr uint64_t CoastalOracleInstanceBytes = CoastalOracleInstanceCount * sizeof(RaySceneInstanceGPU);
	inline constexpr std::string_view CoastalOracleBundleId = "gglab.coastal-retreat";
	inline constexpr std::string_view CoastalOracleBundleRevision =
		"79092da0d167d3bd041de9ead925c45d9f5ed8083cb07f2c0f5351afb94467e9";

	struct CoastalProbeHit
	{
		uint32_t m_Hit = 0;
		uint32_t m_Instance = 0;
		uint32_t m_Primitive = 0;
		uint32_t m_Errors = 0;
		std::array<float, 2> m_Barycentrics{};
		float m_Distance = 0;
		float m_Reserved = 0;
	};
	static_assert(sizeof(CoastalProbeHit) == 32 && offsetof(CoastalProbeHit, m_Barycentrics) == 16 &&
		offsetof(CoastalProbeHit, m_Distance) == 24);

	struct CoastalProbeReference
	{
		uint32_t m_RayIndex;
		uint32_t m_Instance;
		uint32_t m_Primitive;
		float m_BarycentricU;
		float m_BarycentricV;
		float m_Distance;
	};

	// Frozen grid samples for the locked bundle, checked against CPU triangle intersections.
	// Integer identities are exact; floating values allow absolute error across adapters.
	inline constexpr float CoastalOracleBarycentricTolerance = 2e-4f;
	inline constexpr float CoastalOracleDistanceTolerance = 5e-4f;
	inline constexpr std::array CoastalProbeReferences
	{
		CoastalProbeReference{ 839, 1, 347, 0.582849562f, 0.342877179f, 13.8766556f },
		CoastalProbeReference{ 373, 3, 39177, 0.215454012f, 0.284546018f, 14.3199997f },
		CoastalProbeReference{ 589, 6, 58, 0.317460328f, 0.361111134f, 17.6000004f },
		CoastalProbeReference{ 715, 7, 2547, 0.318727493f, 0.361702234f, 13.8402500f },
		CoastalProbeReference{ 19, 20, 24, 0.282837957f, 0.317289591f, 19.5499992f },
		CoastalProbeReference{ 501, 21, 477, 0.282086790f, 0.276190847f, 16.5048809f },
		CoastalProbeReference{ 992, 24, 1, 0.498062491f, 0.498062491f, 20.0499992f },
		CoastalProbeReference{ 698, 25, 49, 0.034486052f, 0.644295573f, 17.7462788f },
		CoastalProbeReference{ 28, 33, 178, 0.912940025f, 0.044101536f, 18.5194492f },
		CoastalProbeReference{ 93, 35, 128, 0.304535866f, 0.420073360f, 19.3050957f },
		CoastalProbeReference{ 156, 36, 214, 0.170329019f, 0.473470837f, 18.7163506f },
		CoastalProbeReference{ 35, 41, 164, 0.216041282f, 0.251507461f, 18.6255531f },
		CoastalProbeReference{ 65, 43, 101, 0.171850517f, 0.311876208f, 19.2382660f },
		CoastalProbeReference{ 162, 44, 108, 0.213570923f, 0.296402633f, 19.0734978f },
		CoastalProbeReference{ 365, 72, 34, 0.038258709f, 0.433965504f, 15.9200001f },
		CoastalProbeReference{ 280, 104, 1102, 0.476112157f, 0.251364559f, 17.4583569f },
		CoastalProbeReference{ 281, 143, 40, 0.664922118f, 0.330681920f, 17.4626961f },
		CoastalProbeReference{ 577, 246, 678, 0.072846696f, 0.553622425f, 17.6449337f },
		CoastalProbeReference{ 363, 350, 8960, 0.134001896f, 0.258454055f, 16.6844521f },
		CoastalProbeReference{ 364, 368, 2170, 0.072506160f, 0.121110357f, 16.5646477f },
		CoastalProbeReference{ 362, 374, 2344, 0.686637104f, 0.143109411f, 16.6486034f },
		CoastalProbeReference{ 361, 411, 1414, 0.722172856f, 0.252971321f, 16.6671276f }
	};

	[[nodiscard]] bool ValidateCoastalRaySceneLayout(const RaySceneDiagnostics& scene,
		std::string& reason) noexcept;
	[[nodiscard]] bool ValidateCoastalRaySceneHits(std::span<const CoastalProbeHit> hits,
		std::string& reason) noexcept;
	[[nodiscard]] bool ValidateCoastalRaySceneTables(std::span<const std::byte> geometries,
		std::span<const std::byte> instances, std::string& reason) noexcept;
}
