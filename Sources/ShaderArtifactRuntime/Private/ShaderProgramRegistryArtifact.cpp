#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool IsValidIdentityComponent(std::string_view value) noexcept
		{
			if (value.empty() || value.size() > MaxShaderProgramIdentityComponentSize)
			{
				return false;
			}
			return std::ranges::find(value, '\0') == value.end();
		}

		[[nodiscard]] auto MakeBindingKey(
			const ShaderProgramRegistryEntry& entry) noexcept
		{
			return std::tie(
				entry.m_ProgramRef.m_ProgramId,
				entry.m_ProgramRef.m_VariantId,
				entry.m_ProgramRef.m_Stage,
				entry.m_TargetProfile);
		}

		[[nodiscard]] bool IsSameBinding(
			const ShaderProgramRegistryEntry& left,
			const ShaderProgramRegistryEntry& right) noexcept
		{
			return MakeBindingKey(left) == MakeBindingKey(right);
		}
	}

	bool IsValidShaderProgramRegistryEntry(
		const ShaderProgramRegistryEntry& entry) noexcept
	{
		return entry.m_ProgramRef.IsValid() &&
			IsValidIdentityComponent(entry.m_ProgramRef.m_ProgramId) &&
			IsValidIdentityComponent(entry.m_ProgramRef.m_VariantId) &&
			IsKnownShaderTargetProfile(entry.m_TargetProfile) &&
			entry.m_ArtifactRef.IsValid();
	}

	ShaderProgramRegistryArtifactId ComputeShaderProgramRegistryArtifactId(
		std::span<const ShaderProgramRegistryEntry> entries) noexcept
	{
		if (entries.empty() || entries.size() > MaxShaderProgramRegistryEntryCount)
		{
			return {};
		}

		Sha256Builder builder;
		bool succeeded =
			builder.AddStringUtf8("gglab.shader.program-registry-artifact-id") &&
			builder.AddU32LE(ShaderProgramRegistryIdentitySchemaVersion) &&
			builder.AddU32LE(ShaderProgramRegistryArtifactSchemaVersion) &&
			builder.AddU32LE(static_cast<uint32_t>(entries.size()));
		for (size_t index = 0; index < entries.size(); ++index)
		{
			const ShaderProgramRegistryEntry& entry = entries[index];
			if (!succeeded || !IsValidShaderProgramRegistryEntry(entry))
			{
				return {};
			}
			if (index > 0 && !(MakeBindingKey(entries[index - 1]) < MakeBindingKey(entry)))
			{
				return {};
			}
			succeeded =
				builder.AddStringUtf8(entry.m_ProgramRef.m_ProgramId) &&
				builder.AddStringUtf8(entry.m_ProgramRef.m_VariantId) &&
				builder.AddU32LE(static_cast<uint32_t>(entry.m_ProgramRef.m_Stage)) &&
				builder.AddU8(static_cast<uint8_t>(entry.m_TargetProfile)) &&
				builder.AddBytes(std::span(
					entry.m_ArtifactRef.m_ArtifactId.m_DurableDigest.m_Value));
		}
		return succeeded
			? ShaderProgramRegistryArtifactId{ .m_DurableDigest = builder.Finish() }
			: ShaderProgramRegistryArtifactId{};
	}

	ShaderProgramRegistryArtifactValidationStatus ValidateShaderProgramRegistryArtifact(
		const ShaderProgramRegistryArtifact& artifact) noexcept
	{
		if (artifact.m_SchemaVersion != ShaderProgramRegistryArtifactSchemaVersion)
		{
			return ShaderProgramRegistryArtifactValidationStatus::UnsupportedSchema;
		}
		if (!artifact.m_RegistryId.IsValid())
		{
			return ShaderProgramRegistryArtifactValidationStatus::InvalidRegistryId;
		}
		if (artifact.m_Entries.empty())
		{
			return ShaderProgramRegistryArtifactValidationStatus::Empty;
		}
		if (artifact.m_Entries.size() > MaxShaderProgramRegistryEntryCount)
		{
			return ShaderProgramRegistryArtifactValidationStatus::TooManyEntries;
		}
		for (size_t index = 0; index < artifact.m_Entries.size(); ++index)
		{
			const ShaderProgramRegistryEntry& entry = artifact.m_Entries[index];
			if (!IsValidShaderProgramRegistryEntry(entry))
			{
				return ShaderProgramRegistryArtifactValidationStatus::InvalidEntry;
			}
			if (index == 0)
			{
				continue;
			}
			const ShaderProgramRegistryEntry& previous = artifact.m_Entries[index - 1];
			if (IsSameBinding(previous, entry))
			{
				return ShaderProgramRegistryArtifactValidationStatus::DuplicateBinding;
			}
			if (!(MakeBindingKey(previous) < MakeBindingKey(entry)))
			{
				return ShaderProgramRegistryArtifactValidationStatus::NonCanonicalOrder;
			}
		}
		return ComputeShaderProgramRegistryArtifactId(artifact.m_Entries) ==
			artifact.m_RegistryId
			? ShaderProgramRegistryArtifactValidationStatus::Valid
			: ShaderProgramRegistryArtifactValidationStatus::InvalidRegistryId;
	}

	ShaderProgramRegistryArtifactBuildResult BuildShaderProgramRegistryArtifact(
		std::span<const ShaderProgramRegistryEntry> entries) noexcept
	{
		if (entries.empty())
		{
			return { .m_Status = ShaderProgramRegistryArtifactBuildStatus::Empty };
		}
		if (entries.size() > MaxShaderProgramRegistryEntryCount)
		{
			return { .m_Status = ShaderProgramRegistryArtifactBuildStatus::TooManyEntries };
		}
		for (const ShaderProgramRegistryEntry& entry : entries)
		{
			if (!IsValidShaderProgramRegistryEntry(entry))
			{
				return { .m_Status = ShaderProgramRegistryArtifactBuildStatus::InvalidEntry };
			}
		}

		try
		{
			ShaderProgramRegistryArtifact artifact{};
			artifact.m_Entries.assign(entries.begin(), entries.end());
			std::ranges::sort(artifact.m_Entries,
				[](const ShaderProgramRegistryEntry& left,
					const ShaderProgramRegistryEntry& right) noexcept
				{ return MakeBindingKey(left) < MakeBindingKey(right); });
			for (size_t index = 1; index < artifact.m_Entries.size(); ++index)
			{
				if (IsSameBinding(artifact.m_Entries[index - 1], artifact.m_Entries[index]))
				{
					return {
						.m_Status = ShaderProgramRegistryArtifactBuildStatus::DuplicateBinding,
					};
				}
			}
			artifact.m_RegistryId =
				ComputeShaderProgramRegistryArtifactId(artifact.m_Entries);
			if (!artifact.m_RegistryId.IsValid())
			{
				return { .m_Status = ShaderProgramRegistryArtifactBuildStatus::Failed };
			}
			return {
				.m_Status = ShaderProgramRegistryArtifactBuildStatus::Built,
				.m_Artifact = std::move(artifact),
			};
		}
		catch (...)
		{
			return { .m_Status = ShaderProgramRegistryArtifactBuildStatus::Failed };
		}
	}

	std::optional<ShaderArtifactRef> ResolveShaderProgramRegistryArtifact(
		const ShaderProgramRegistryArtifact& artifact,
		const ShaderProgramRef& programRef,
		ShaderTargetProfile targetProfile) noexcept
	{
		if (!programRef.IsValid() || !IsKnownShaderTargetProfile(targetProfile) ||
			ValidateShaderProgramRegistryArtifact(artifact) !=
				ShaderProgramRegistryArtifactValidationStatus::Valid)
		{
			return std::nullopt;
		}
		const auto requestedKey = std::tie(
			programRef.m_ProgramId,
			programRef.m_VariantId,
			programRef.m_Stage,
			targetProfile);
		const auto iterator = std::ranges::lower_bound(
			artifact.m_Entries,
			requestedKey,
			{},
			[](const ShaderProgramRegistryEntry& entry) noexcept
			{ return MakeBindingKey(entry); });
		return iterator != artifact.m_Entries.end() &&
			MakeBindingKey(*iterator) == requestedKey
			? std::optional<ShaderArtifactRef>(iterator->m_ArtifactRef)
			: std::nullopt;
	}
}
