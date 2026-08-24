#pragma once
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t ShaderProgramRegistryIdentitySchemaVersion = 1;
	inline constexpr uint32_t ShaderProgramRegistryArtifactSchemaVersion = 1;
	inline constexpr size_t MaxShaderProgramIdentityComponentSize = 1024;
	inline constexpr uint32_t MaxShaderProgramRegistryEntryCount = 65536;

	struct ShaderProgramRegistryArtifactId final
	{
		Sha256Digest m_DurableDigest{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_DurableDigest.IsValid();
		}

		friend constexpr bool operator==(
			const ShaderProgramRegistryArtifactId&,
			const ShaderProgramRegistryArtifactId&) noexcept = default;
	};

	struct ShaderProgramRegistryArtifactRef final
	{
		ShaderProgramRegistryArtifactId m_RegistryId{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_RegistryId.IsValid();
		}

		friend constexpr bool operator==(
			const ShaderProgramRegistryArtifactRef&,
			const ShaderProgramRegistryArtifactRef&) noexcept = default;
	};

	struct ShaderProgramRegistryEntry final
	{
		ShaderProgramRef m_ProgramRef{};
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
		ShaderArtifactRef m_ArtifactRef{};

		friend bool operator==(
			const ShaderProgramRegistryEntry&,
			const ShaderProgramRegistryEntry&) noexcept = default;
	};

	struct ShaderProgramRegistryArtifact final
	{
		uint32_t m_SchemaVersion = ShaderProgramRegistryArtifactSchemaVersion;
		ShaderProgramRegistryArtifactId m_RegistryId{};
		std::vector<ShaderProgramRegistryEntry> m_Entries{};
	};

	enum class ShaderProgramRegistryArtifactValidationStatus : uint8_t
	{
		Valid,
		UnsupportedSchema,
		InvalidRegistryId,
		InvalidEntry,
		Empty,
		TooManyEntries,
		NonCanonicalOrder,
		DuplicateBinding,
	};

	enum class ShaderProgramRegistryArtifactBuildStatus : uint8_t
	{
		Built,
		InvalidEntry,
		Empty,
		TooManyEntries,
		DuplicateBinding,
		Failed,
	};

	struct ShaderProgramRegistryArtifactBuildResult final
	{
		ShaderProgramRegistryArtifactBuildStatus m_Status =
			ShaderProgramRegistryArtifactBuildStatus::Failed;
		ShaderProgramRegistryArtifact m_Artifact{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderProgramRegistryArtifactBuildStatus::Built;
		}
	};

	[[nodiscard]] bool IsValidShaderProgramRegistryEntry(
		const ShaderProgramRegistryEntry& entry) noexcept;
	[[nodiscard]] ShaderProgramRegistryArtifactId ComputeShaderProgramRegistryArtifactId(
		std::span<const ShaderProgramRegistryEntry> entries) noexcept;
	[[nodiscard]] ShaderProgramRegistryArtifactValidationStatus
		ValidateShaderProgramRegistryArtifact(
			const ShaderProgramRegistryArtifact& artifact) noexcept;
	[[nodiscard]] ShaderProgramRegistryArtifactBuildResult
		BuildShaderProgramRegistryArtifact(
			std::span<const ShaderProgramRegistryEntry> entries) noexcept;
	[[nodiscard]] std::optional<ShaderArtifactRef> ResolveShaderProgramRegistryArtifact(
		const ShaderProgramRegistryArtifact& artifact,
		const ShaderProgramRef& programRef,
		ShaderTargetProfile targetProfile) noexcept;
}
