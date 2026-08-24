#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"
#include "ShaderArtifactRuntime/ShaderArtifactManifest.h"

#include <span>
#include <string>
#include <utility>

namespace gglab
{
	ShaderArtifactId ComputeShaderArtifactId(
		const ShaderRuntimeArtifactManifest& manifest) noexcept
	{
		if (!IsValidShaderRuntimeEntryPoint(manifest.m_EntryPoint))
		{
			return {};
		}

		Sha256Builder builder;
		const bool succeeded =
			builder.AddStringUtf8("gglab.shader.runtime-artifact-id") &&
			builder.AddU32LE(ShaderRuntimeArtifactIdentitySchemaVersion) &&
			builder.AddU32LE(manifest.m_SchemaVersion) &&
			builder.AddU8(static_cast<uint8_t>(manifest.m_TargetProfile)) &&
			builder.AddU8(static_cast<uint8_t>(manifest.m_BinaryFormat)) &&
			builder.AddU8(static_cast<uint8_t>(manifest.m_SpirVTargetEnvironment)) &&
			builder.AddU32LE(manifest.m_BindingABIRevision) &&
			builder.AddU8(static_cast<uint8_t>(manifest.m_CoordinateOptions)) &&
			builder.AddU32LE(static_cast<uint32_t>(manifest.m_Stage)) &&
			builder.AddStringUtf8(manifest.m_EntryPoint) &&
			builder.AddBytes(std::span(manifest.m_BinaryContentDigest.m_Digest.m_Value));
		if (!succeeded)
		{
			return {};
		}

		return ShaderArtifactId{ .m_DurableDigest = builder.Finish() };
	}

	ShaderRuntimeArtifactManifest BuildShaderRuntimeArtifactManifest(
		const ShaderArtifactManifest& manifest) noexcept
	{
		std::string entryPoint;
		try
		{
			if (manifest.m_EntryPoint.empty() ||
				manifest.m_EntryPoint.size() > MaxShaderRuntimeEntryPointSize)
			{
				return {};
			}
			entryPoint.reserve(manifest.m_EntryPoint.size());
			for (const wchar_t character : manifest.m_EntryPoint)
			{
				if (character <= 0 || character > 0x7f)
				{
					return {};
				}
				entryPoint.push_back(static_cast<char>(character));
			}
		}
		catch (...)
		{
			return {};
		}

		ShaderRuntimeArtifactManifest runtimeManifest{
			.m_TargetProfile = manifest.m_TargetProfile,
			.m_BinaryFormat = manifest.m_BinaryFormat,
			.m_SpirVTargetEnvironment = manifest.m_SpirVTargetEnvironment,
			.m_BindingABIRevision = manifest.m_BindingABIRevision,
			.m_CoordinateOptions = manifest.m_CoordinateOptions,
			.m_Stage = manifest.m_Stage,
			.m_EntryPoint = std::move(entryPoint),
			.m_BinaryContentDigest = manifest.m_BinaryContentDigest,
		};
		runtimeManifest.m_ArtifactId = ComputeShaderArtifactId(runtimeManifest);
		return runtimeManifest;
	}
}
