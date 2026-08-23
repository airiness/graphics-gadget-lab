#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactManifest.h"

namespace gglab
{
	// Toolchain output / Runtime input. The manifest carries the authoritative
	// identity and validation fields; the binary holds the exact committed
	// DXIL/SPIR-V bytes.
	struct ShaderArtifact
	{
		ShaderArtifactManifest m_Manifest{};
		ShaderBinary m_Binary{};

		[[nodiscard]] ShaderBinaryFormat GetBinaryFormat() const noexcept
		{
			return m_Manifest.m_BinaryFormat;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Binary.IsValid() && m_Manifest.m_BinaryContentDigest.m_Digest.IsValid();
		}
	};

	[[nodiscard]] bool IsShaderBinaryFormat(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept;
	[[nodiscard]] ShaderHash128 ComputeShaderBinaryHash(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept;
}
