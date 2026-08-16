#pragma once
#include "Contracts/ShaderArtifactManifest.h"
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"

#include <string>
#include <vector>

namespace gglab
{
	// Toolchain output / Runtime input. The manifest carries the authoritative
	// identity and validation fields; the binary holds the exact published
	// GPU bytes.
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

	enum class ShaderCompileStatus : uint8_t
	{
		Success,
		InvalidRequest,
		SourceNotFound,
		CompilerUnavailable,
		CompileFailed,
		ArtifactIOFailure,
	};

	// Compile failure is a normal Toolchain outcome, not a fatal Renderer
	// invariant: structured status, an optional validation error code, the
	// raw message, and the source identity.
	struct ShaderCompilerDiagnostics
	{
		ShaderCompileStatus m_Status = ShaderCompileStatus::Success;
		ShaderCompileValidationError m_ValidationError = ShaderCompileValidationError::None;
		std::wstring m_Message{};
		std::wstring m_SourceIdentity{};

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Status == ShaderCompileStatus::Success;
		}
	};

	struct ShaderCompileResult
	{
		ShaderCompileStatus m_Status = ShaderCompileStatus::Success;
		ShaderArtifact m_Artifact{};
		ShaderRecipeId m_RecipeId{};
		bool m_FromCache = false;
		ShaderCompilerDiagnostics m_Diagnostics{};

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Status == ShaderCompileStatus::Success;
		}
	};

	// Fully resolved compile authority. The runtime keeps raw caller
	// descriptions only as an origin/display view; compile, diagnostics,
	// reload, and recipe identity all use this representation.
	struct ShaderResolvedRecipe
	{
		ShaderCompileStatus m_Status = ShaderCompileStatus::Success;
		ShaderCompilerDiagnostics m_Diagnostics{};
		ShaderDesc m_Request{};
		ShaderCompilerIdentity m_CompilerIdentity{};
		ShaderRecipeId m_RecipeId{};
		LocalShaderCacheKey m_BuildKey{};
		std::vector<std::wstring> m_CompileArguments{};

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Status == ShaderCompileStatus::Success;
		}
	};

	// Binary content utilities. The 128-bit hash is runtime convenience
	// identity (pipeline-facing change detection); durable identity is the
	// manifest BinaryContentDigest.
	[[nodiscard]] bool IsShaderBinaryFormat(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept;
	[[nodiscard]] ShaderHash128 ComputeShaderBinaryHash(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept;
}
