#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t ShaderRecipeHashSchema = 1;
	inline constexpr uint32_t ShaderCacheMetadataSchema = 3;

	// Canonical logical shader request identity ("which variant").
	struct ShaderRecipeId final
	{
		ShaderHash128 m_Digest{};
		constexpr bool operator==(const ShaderRecipeId&) const noexcept = default;
	};

	// Producer/dependency-sensitive build reuse identity. S2 composition:
	// ShaderRecipeId + compiler identity. Dependency records keep the S2
	// physical mtime form and are validated at cache-hit time.
	struct LocalShaderCacheKey final
	{
		ShaderHash128 m_Digest{};
		constexpr bool operator==(const LocalShaderCacheKey&) const noexcept = default;
	};

	// SHA-256 of the exact bytes published as the DXIL/SPIR-V binary. This is
	// the authoritative content-validation identity for cache hits and
	// corruption detection; DXIL container hashes and runtime convenience
	// hashes must not be substituted for it.
	struct BinaryContentDigest final
	{
		Sha256Digest m_Digest{};

		friend constexpr bool operator==(
			const BinaryContentDigest&, const BinaryContentDigest&) noexcept = default;
	};

	// S2 dependency validation record: physical path + last-write ticks.
	// Content digests and portable dependency identity belong to S3.
	struct ShaderArtifactDependency
	{
		std::filesystem::path m_Path{};
		int64_t m_LastWriteTimeTicks = 0;
	};

	// Authoritative in-memory artifact manifest model owned by the Shader
	// Toolchain. S2 serializes it losslessly through the key=value
	// (*.meta.txt, schema=3) serializer; S3 only replaces the serialized
	// representation.
	struct ShaderArtifactManifest
	{
		uint32_t m_SchemaVersion = ShaderCacheMetadataSchema;
		uint32_t m_RecipeHashSchema = ShaderRecipeHashSchema;
		ShaderRecipeId m_RecipeId{};
		LocalShaderCacheKey m_BuildKey{};
		ShaderCompilerIdentity m_CompilerIdentity{};
		ShaderBinaryFormat m_BinaryFormat = ShaderBinaryFormat::Unknown;
		ShaderSpirVTargetEnvironment m_SpirVTargetEnvironment =
			ShaderSpirVTargetEnvironment::None;
		uint32_t m_BindingABIRevision = 0;
		ShaderCoordinateOptions m_CoordinateOptions = ShaderCoordinateOptions::None;
		ShaderStage m_Stage = ShaderStage::Vertex;
		std::filesystem::path m_SourcePath{};
		std::wstring m_EntryPoint{};
		std::wstring m_TargetString{};
		std::vector<std::wstring> m_Defines{};
		std::vector<std::filesystem::path> m_IncludeDirs{};
		std::vector<std::wstring> m_ExtraArgs{};
		std::vector<ShaderArtifactDependency> m_Dependencies{};
		BinaryContentDigest m_BinaryContentDigest{};
	};
}
