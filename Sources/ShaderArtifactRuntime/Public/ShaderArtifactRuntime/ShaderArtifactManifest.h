#pragma once
#include "GGLabFoundation/Hash/Sha256.h"
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t ShaderRecipeHashSchema = 1;
	inline constexpr uint32_t ShaderArtifactManifestSchemaVersion = 2;

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

	struct ShaderArtifactDependency
	{
		std::filesystem::path m_LogicalPath{};
		Sha256Digest m_ContentDigest{};

		friend constexpr bool operator==(
			const ShaderArtifactDependency&, const ShaderArtifactDependency&) noexcept = default;
	};

	// Portable artifact manifest. Runtime consumers may ignore producer/source
	// fields, but compatibility and identity fields remain one versioned
	// producer/runtime boundary contract.
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
		std::filesystem::path m_LogicalSourcePath{};
		std::wstring m_EntryPoint{};
		std::wstring m_TargetString{};
		std::vector<std::wstring> m_Defines{};
		std::vector<std::filesystem::path> m_LogicalIncludeDirs{};
		std::vector<std::wstring> m_ExtraArgs{};
		BinaryContentDigest m_BinaryContentDigest{};
		std::vector<ShaderArtifactDependency> m_Dependencies{};

		friend constexpr bool operator==(
			const ShaderArtifactManifest&, const ShaderArtifactManifest&) noexcept = default;
	};
}
