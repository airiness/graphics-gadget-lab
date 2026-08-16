#pragma once
#include "Contracts/ShaderArtifactManifest.h"

#include <filesystem>
#include <optional>

namespace gglab
{
	// Versioned, machine-readable serializer for the local shader cache entry
	// (*.shaderartifact.json, schemaVersion=1). The document carries the
	// portable ShaderArtifactManifest plus the machine-local validation state
	// (physical paths, dependency mtime/digest records). Unknown schema
	// versions or malformed documents are rejected so callers treat them as
	// cache misses.
	[[nodiscard]] bool WriteShaderArtifactCacheRecord(
		const std::filesystem::path& manifestPath,
		const ShaderArtifactCacheRecord& record) noexcept;

	// Parses and structurally validates a cache record document. Returns
	// nullopt on any failure (missing file, unknown schema, missing/malformed
	// field): callers treat that as a cache miss / invalid derived data.
	[[nodiscard]] std::optional<ShaderArtifactCacheRecord> ReadShaderArtifactCacheRecord(
		const std::filesystem::path& manifestPath) noexcept;

	// Reader side of the publication protocol: the manifest document is the
	// commit record, the binary is verified against the manifest digest. Any
	// failure (missing/unreadable files, schema mismatch, digest mismatch,
	// TOCTOU) is a cache miss, never fatal.
	[[nodiscard]] std::optional<ShaderArtifactCacheRecord> LoadShaderArtifactCacheRecord(
		const std::filesystem::path& manifestPath,
		const std::filesystem::path& binaryPath) noexcept;

	// Writer side of the publication protocol: writes a unique temporary
	// binary and manifest document, validates the complete result, publishes
	// the immutable binary first and the manifest last as the commit record.
	// Concurrent winners converge on equivalent content; a partial or
	// orphaned entry can never be consumed. Returns false only when the
	// publication could not complete (recoverable ArtifactIOFailure).
	[[nodiscard]] bool PublishShaderArtifactCacheRecord(
		const std::filesystem::path& binaryPath,
		const std::filesystem::path& manifestPath,
		const ShaderArtifactCacheRecord& record) noexcept;
}
