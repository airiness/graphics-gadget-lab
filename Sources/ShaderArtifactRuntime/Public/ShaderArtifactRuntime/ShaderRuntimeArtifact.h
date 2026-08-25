#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <cstdint>
#include <string>

namespace gglab
{
	struct ShaderArtifactManifest;

	inline constexpr uint32_t ShaderRuntimeArtifactIdentitySchemaVersion = 2;
	inline constexpr uint32_t ShaderRuntimeArtifactManifestSchemaVersion = 2;

	struct ShaderArtifactId final
	{
		Sha256Digest m_DurableDigest{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_DurableDigest.IsValid();
		}

		friend constexpr bool operator==(
			const ShaderArtifactId&, const ShaderArtifactId&) noexcept = default;
	};

	struct ShaderArtifactRef final
	{
		ShaderArtifactId m_ArtifactId{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_ArtifactId.IsValid();
		}

		friend constexpr bool operator==(
			const ShaderArtifactRef&, const ShaderArtifactRef&) noexcept = default;
	};

	// Portable Runtime-only manifest. Build provenance and machine-local cache
	// state deliberately do not cross this boundary.
	struct ShaderRuntimeArtifactManifest final
	{
		uint32_t m_SchemaVersion = ShaderRuntimeArtifactManifestSchemaVersion;
		ShaderArtifactId m_ArtifactId{};
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
		ShaderBinaryFormat m_BinaryFormat = ShaderBinaryFormat::Unknown;
		ShaderSpirVTargetEnvironment m_SpirVTargetEnvironment =
			ShaderSpirVTargetEnvironment::None;
		uint32_t m_BindingABIRevision = 0;
		ShaderCoordinateOptions m_CoordinateOptions = ShaderCoordinateOptions::None;
		ShaderStage m_Stage = ShaderStage::Vertex;
		std::string m_EntryPoint{};
		BinaryContentDigest m_BinaryContentDigest{};

		friend constexpr bool operator==(
			const ShaderRuntimeArtifactManifest&,
			const ShaderRuntimeArtifactManifest&) noexcept = default;
	};

	struct ShaderRuntimeArtifact final
	{
		ShaderRuntimeArtifactManifest m_Manifest{};
		ShaderBinary m_Binary{};
	};

	// Stable content address over the complete Runtime-visible artifact
	// contract. The stored m_ArtifactId field is intentionally excluded.
	[[nodiscard]] ShaderArtifactId ComputeShaderArtifactId(
		const ShaderRuntimeArtifactManifest& manifest) noexcept;
	[[nodiscard]] ShaderRuntimeArtifactManifest BuildShaderRuntimeArtifactManifest(
		const ShaderArtifactManifest& manifest) noexcept;
}
