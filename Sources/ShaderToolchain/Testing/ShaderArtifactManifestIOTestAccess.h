#pragma once
#include "Artifact/ShaderArtifactManifestIO.h"

#include <filesystem>
#include <optional>

namespace gglab::testing
{
	// Exact bytes and digest returned by the single cache binary read point.
	struct BinaryReadWithDigest
	{
		ShaderBinary m_Binary{};
		Sha256Digest m_Digest{};
	};

	[[nodiscard]] std::optional<BinaryReadWithDigest> ReadBinaryWithDigestOnce(
		const std::filesystem::path& path) noexcept;

	// Replaces the binary read point so contract tests can prove that cache
	// validation hashes exactly the one set of bytes returned to the caller.
	// Passing null restores production behavior. Not safe during concurrent
	// shader compilation.
	using BinaryReadOnceOverride = std::optional<BinaryReadWithDigest>(*)(
		const std::filesystem::path& path) noexcept;
	void OverrideBinaryReadOnce(BinaryReadOnceOverride overrideFn) noexcept;

	// Injects failures into publication rename steps while retaining the real
	// filesystem implementation for successful operations. Passing null
	// restores production behavior. Not safe during concurrent compilation.
	using PublishFileFailureInjector = bool(*)(
		const std::filesystem::path& destination) noexcept;
	void OverridePublishFileFailure(PublishFileFailureInjector injector) noexcept;
}
