#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t ShaderRecipeHashSchema = 1;
	// Portable manifest schema: versioned by the portable field set only
	// (recipe semantics, identity algorithm, compiler identity expression,
	// dependency provenance expression). Schema 2 adds the portable
	// dependencies field.
	inline constexpr uint32_t ShaderArtifactManifestSchemaVersion = 2;
	// Local cache record schema: versioned by the document root structure and
	// the machine-local fields (physical resolutions, dependency physical
	// mapping). Schema 2 introduces the root-level version key, moves
	// dependency provenance into the manifest, replaces the legacy combined
	// dependency records with index-corresponding physical paths, and drops
	// the unused mtime field. Local evolution never bumps the manifest
	// schema, and portable evolution never bumps the record schema.
	inline constexpr uint32_t ShaderArtifactCacheRecordSchemaVersion = 2;

	// Fast-hash derivation for bucket/lookup acceleration only. Identity
	// equality must always compare the full durable digest.
	[[nodiscard]] constexpr ShaderHash128 ShaderDigestFastHash(
		const Sha256Digest& digest) noexcept
	{
		const auto readU64 = [&digest](std::size_t offset) noexcept
			{
				std::uint64_t value = 0;
				for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
				{
					value = (value << 8u) |
						std::to_integer<std::uint64_t>(digest.m_Value[offset + byteIndex]);
				}
				return value;
			};
		return {
			.m_LowBits = readU64(sizeof(std::uint64_t)),
			.m_HighBits = readU64(0),
		};
	}

	// Canonical logical shader request identity ("which variant"). Durable
	// SHA-256 over the logical request only: logical source identity, entry,
	// stage, target semantics, defines, logical include configuration, and
	// extra compile options. Physical checkout locations never participate.
	struct ShaderRecipeId final
	{
		Sha256Digest m_DurableDigest{};

		[[nodiscard]] constexpr ShaderHash128 GetFastHash() const noexcept
		{
			return ShaderDigestFastHash(m_DurableDigest);
		}

		friend constexpr bool operator==(
			const ShaderRecipeId&, const ShaderRecipeId&) noexcept = default;
	};

	// Producer/dependency-sensitive build reuse identity: the recipe identity
	// (durable digest) plus the compiler identity. Dependency validation is
	// content-based (see ShaderArtifactDependency); the key itself stays
	// producer-stable so the same recipe reuses the entry across identical
	// producers.
	struct LocalShaderCacheKey final
	{
		Sha256Digest m_DurableDigest{};

		[[nodiscard]] constexpr ShaderHash128 GetFastHash() const noexcept
		{
			return ShaderDigestFastHash(m_DurableDigest);
		}

		friend constexpr bool operator==(
			const LocalShaderCacheKey&, const LocalShaderCacheKey&) noexcept = default;
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

	// Portable dependency provenance record. The content digest is the sole
	// validation authority: it is computed over the exact bytes handed to the
	// compiler, so acceptance can never drift from what was actually
	// compiled. The logical path is the portable identity when derivable
	// (relative to the source root; empty for includes outside it). Physical
	// resolution is local cache state and lives in
	// ShaderArtifactCacheRecord::m_DependencyPhysicalPaths, never in the
	// portable manifest.
	struct ShaderArtifactDependency
	{
		std::filesystem::path m_LogicalPath{};
		Sha256Digest m_ContentDigest{};
	};

	// Portable artifact manifest: the compile semantics and identities that
	// describe the artifact itself. It must never carry checkout physical
	// paths or local validation state.
	struct ShaderArtifactManifest
	{
		uint32_t m_SchemaVersion = ShaderArtifactManifestSchemaVersion;
		uint32_t m_RecipeHashSchema = ShaderRecipeHashSchema;
		ShaderRecipeId m_RecipeId{};
		LocalShaderCacheKey m_BuildKey{};
		ShaderCompilerIdentity m_CompilerIdentity{};
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
		ShaderBinaryFormat m_BinaryFormat = ShaderBinaryFormat::Unknown;
		ShaderSpirVTargetEnvironment m_SpirVTargetEnvironment =
			ShaderSpirVTargetEnvironment::None;
		uint32_t m_BindingABIRevision = 0;
		ShaderCoordinateOptions m_CoordinateOptions = ShaderCoordinateOptions::None;
		ShaderStage m_Stage = ShaderStage::Vertex;
		ShaderModel m_ShaderModel = ShaderModel::SM_6_7;
		std::wstring m_HlslVersion{ L"2021" };
		ShaderCompileFlags m_CompileFlags = ShaderCompileFlags::None;
		std::wstring m_OptimizationLevel{ L"O3" };
		// Normative source identity: logical path relative to the source root.
		std::filesystem::path m_LogicalSourcePath{};
		std::wstring m_EntryPoint{};
		std::wstring m_TargetString{};
		std::vector<std::wstring> m_Defines{};
		std::vector<std::filesystem::path> m_LogicalIncludeDirs{};
		std::vector<std::wstring> m_ExtraArgs{};
		BinaryContentDigest m_BinaryContentDigest{};
		// Portable dependency provenance: what logical dependencies (logical
		// path + exact consumed content digest) this artifact was compiled
		// from. The main source is the first element. No physical paths.
		std::vector<ShaderArtifactDependency> m_Dependencies{};
	};

	// Local cache record: the portable manifest plus the derived local state
	// used to validate and rebuild the cache entry on this machine. The
	// physical fields are never part of the portable artifact contract.
	struct ShaderArtifactCacheRecord
	{
		ShaderArtifactManifest m_Manifest{};
		ShaderBinary m_Binary{};
		std::filesystem::path m_PhysicalSourcePath{};
		std::vector<std::filesystem::path> m_PhysicalIncludeDirs{};
		// Physical resolution for each portable dependency, index-corresponding
		// with m_Manifest.m_Dependencies. Record parse/validation requires
		// both lists to have identical cardinality.
		std::vector<std::filesystem::path> m_DependencyPhysicalPaths{};
	};
}
