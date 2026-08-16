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
	inline constexpr uint32_t ShaderArtifactManifestSchemaVersion = 1;

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
	// SHA-256; persisted serializations use the full digest, never the
	// truncated fast hash.
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
	// (durable digest) plus the compiler identity. Dependency records keep
	// the physical mtime form and are validated at cache-hit time.
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

	// Dependency validation record: physical path + last-write ticks.
	struct ShaderArtifactDependency
	{
		std::filesystem::path m_Path{};
		int64_t m_LastWriteTimeTicks = 0;
	};

	// Authoritative in-memory artifact manifest model owned by the Shader
	// Toolchain. Its serialized form is a versioned, machine-readable JSON
	// document (*.shaderartifact.json, schemaVersion=1).
	struct ShaderArtifactManifest
	{
		uint32_t m_SchemaVersion = ShaderArtifactManifestSchemaVersion;
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
		// Normative source identity: logical path relative to the source root
		// (e.g. Passes/PassForwardPBR.hlsl). The physical path below is used
		// for IO and diagnostics only and never participates in the recipe
		// identity.
		std::filesystem::path m_LogicalSourcePath{};
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
