#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactManifest.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gglab
{
	// Local cache record schema: versioned by the document root structure and
	// the machine-local fields (physical resolutions, dependency physical
	// mapping). Local evolution never bumps the portable manifest schema, and
	// portable evolution never bumps the local record schema.
	inline constexpr uint32_t ShaderArtifactCacheRecordSchemaVersion = 2;

	// Toolchain-local cache record. Physical source resolution and validation
	// state never cross the artifact runtime public contract.
	struct ShaderArtifactCacheRecord
	{
		ShaderArtifactManifest m_Manifest{};
		ShaderBinary m_Binary{};
		std::filesystem::path m_PhysicalSourcePath{};
		std::vector<std::filesystem::path> m_PhysicalIncludeDirs{};
		// Physical resolution for each portable dependency, index-corresponding
		// with m_Manifest.m_Dependencies. Record parse/validation requires both
		// lists to have identical cardinality.
		std::vector<std::filesystem::path> m_DependencyPhysicalPaths{};
	};
}
