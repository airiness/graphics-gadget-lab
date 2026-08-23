#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"
#include "ShaderArtifactRuntime/ShaderArtifactManifest.h"

#include <span>

namespace gglab
{
	ShaderArtifactId ComputeShaderArtifactId(
		const ShaderRuntimeArtifactManifest& manifest) noexcept
	{
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
		ShaderRuntimeArtifactManifest runtimeManifest{
			.m_TargetProfile = manifest.m_TargetProfile,
			.m_BinaryFormat = manifest.m_BinaryFormat,
			.m_SpirVTargetEnvironment = manifest.m_SpirVTargetEnvironment,
			.m_BindingABIRevision = manifest.m_BindingABIRevision,
			.m_CoordinateOptions = manifest.m_CoordinateOptions,
			.m_Stage = manifest.m_Stage,
			.m_BinaryContentDigest = manifest.m_BinaryContentDigest,
		};
		runtimeManifest.m_ArtifactId = ComputeShaderArtifactId(runtimeManifest);
		return runtimeManifest;
	}
}
