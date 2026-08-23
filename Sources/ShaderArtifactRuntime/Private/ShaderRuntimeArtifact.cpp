#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"

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
}
