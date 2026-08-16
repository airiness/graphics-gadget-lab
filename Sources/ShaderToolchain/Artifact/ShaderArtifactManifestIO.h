#pragma once
#include "Contracts/ShaderArtifact.h"
#include "Contracts/ShaderArtifactManifest.h"

#include <filesystem>
#include <optional>

namespace gglab
{
	// Key=value serializer for the S2 manifest model (*.meta.txt, schema=3).
	// S3 will replace the serialized representation, not this model.
	[[nodiscard]] bool WriteShaderArtifactManifest(
		const std::filesystem::path& manifestPath,
		const ShaderArtifactManifest& manifest) noexcept;

	// Parses and structurally validates a manifest. Returns nullopt on any
	// failure (unknown schema, missing required field, malformed record):
	// callers treat that as a cache miss / invalid derived data.
	[[nodiscard]] std::optional<ShaderArtifactManifest> ReadShaderArtifactManifest(
		const std::filesystem::path& manifestPath) noexcept;

	// Reader side of the publication protocol: the manifest is the commit
	// record, the binary is verified against the manifest digest. Any failure
	// (missing/unreadable files, schema mismatch, digest mismatch, TOCTOU) is
	// a cache miss, never fatal.
	[[nodiscard]] std::optional<ShaderArtifact> LoadShaderArtifact(
		const std::filesystem::path& manifestPath,
		const std::filesystem::path& binaryPath) noexcept;

	// Writer side of the publication protocol: writes a unique temporary
	// binary and manifest, validates the complete result, publishes the
	// immutable binary first and the manifest last as the commit record.
	// Concurrent winners converge on equivalent content; a partial or
	// orphaned entry can never be consumed. Returns false only when the
	// publication could not complete (recoverable ArtifactIOFailure).
	[[nodiscard]] bool PublishShaderArtifact(
		const std::filesystem::path& binaryPath,
		const std::filesystem::path& manifestPath,
		const ShaderArtifact& artifact) noexcept;
}
