#pragma once
#include "Contracts/ShaderArtifactManifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	// Canonical portable-manifest serialization boundary. This is the same
	// schema, semantics, and JSON mapping used by the manifest sub-object of a
	// cache record; it contains portable dependency provenance and no local
	// physical resolution state. Textual byte identity with the nested form is
	// not part of the contract.
	[[nodiscard]] std::optional<std::string> SerializeShaderArtifactManifest(
		const ShaderArtifactManifest& manifest) noexcept;
	[[nodiscard]] std::optional<ShaderArtifactManifest> DeserializeShaderArtifactManifest(
		std::string_view serializedManifest) noexcept;

	// Versioned, machine-readable serializer for the local shader cache record
	// (<binary-name>.json, record schema 2). The document carries the portable
	// ShaderArtifactManifest plus machine-local physical resolution state.
	// Unknown schema versions or malformed documents are rejected so callers
	// treat them as cache misses.
	[[nodiscard]] bool WriteShaderArtifactCacheRecord(
		const std::filesystem::path& recordPath,
		const ShaderArtifactCacheRecord& record) noexcept;

	// Parses and structurally validates a cache record document. Returns
	// nullopt on any failure (missing file, unknown schema, missing/malformed
	// field): callers treat that as a cache miss / invalid derived data.
	[[nodiscard]] std::optional<ShaderArtifactCacheRecord> ReadShaderArtifactCacheRecord(
		const std::filesystem::path& recordPath) noexcept;

	// Binary read-once result: the exact loaded bytes plus the SHA-256 of
	// those same in-memory bytes. The digest is computed from the returned
	// bytes only, so the validated content and the returned content can never
	// diverge.
	struct BinaryReadWithDigest
	{
		ShaderBinary m_Binary{};
		Sha256Digest m_Digest{};
	};

	// The single binary read point of LoadShaderArtifactCacheRecord: reads the
	// file once, hashes exactly the loaded bytes, and returns both.
	[[nodiscard]] std::optional<BinaryReadWithDigest> ReadBinaryWithDigestOnce(
		const std::filesystem::path& path) noexcept;

	// Test seam: replace the binary read point of LoadShaderArtifactCacheRecord
	// with a scripted reader so contract tests can deterministically prove the
	// single-read invariant (exactly one read, and the validated digest
	// describes the returned bytes). Passing a null function restores the
	// production implementation. Test-only; not thread-safe against concurrent
	// compile calls.
	using BinaryReadOnceOverride = std::optional<BinaryReadWithDigest>(*)(
		const std::filesystem::path& path) noexcept;
	void OverrideBinaryReadOnceForTest(BinaryReadOnceOverride overrideFn) noexcept;

	// Publication outcome, classified by the final committed-entry observation
	// (never by whether this call's own renames succeeded):
	//   Published        - the observed structurally valid committed entry is
	//                      content-equivalent to this operation's product.
	//   CommittedByOther - the observed structurally valid same-slot entry
	//                      belongs to another/non-equivalent producer.
	//   Failed           - no structurally valid committed entry was observed.
	enum class ShaderPublicationOutcome : uint8_t
	{
		Published,
		CommittedByOther,
		Failed,
	};

	// Publication result: success hands off the committed record that the
	// publication operation actually observed and structurally validated.
	struct ShaderPublicationResult
	{
		ShaderPublicationOutcome m_Outcome = ShaderPublicationOutcome::Failed;
		ShaderArtifactCacheRecord m_CommittedRecord{};

		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return m_Outcome != ShaderPublicationOutcome::Failed;
		}
	};

	// Test seam: inject scripted failures into the publication rename steps
	// without replacing the successful rename implementation, so contract
	// tests can deterministically cover the failure branches and the final
	// observation classification. The injector receives the destination path
	// and returns true to fail that rename. Passing a null function restores
	// the production implementation. Test-only; not thread-safe against
	// concurrent compile calls.
	using PublishFileFailureInjector = bool(*)(
		const std::filesystem::path& destination) noexcept;
	void OverridePublishFileFailureForTest(PublishFileFailureInjector injector) noexcept;

	// Reader side of the publication protocol: the cache record document is the
	// commit marker. The binary is read exactly once; the manifest
	// BinaryContentDigest is compared against the SHA-256 of those exact
	// in-memory bytes, and those same bytes are returned. Any failure
	// (missing/unreadable files, schema mismatch, digest mismatch) is a cache
	// miss, never fatal.
	[[nodiscard]] std::optional<ShaderArtifactCacheRecord> LoadShaderArtifactCacheRecord(
		const std::filesystem::path& recordPath,
		const std::filesystem::path& binaryPath) noexcept;

	// Writer side of the publication protocol: writes a unique temporary
	// binary and cache record document, attempts to publish the immutable binary
	// first and the cache record last as the commit marker, then performs the
	// final committed-entry observation and classifies the outcome. Success
	// hands off the observed structurally validated committed record; it does
	// not promise that this operation's product is the filesystem writer.
	// Failed means no structurally valid committed entry was observed
	// (recoverable ArtifactIOFailure).
	[[nodiscard]] ShaderPublicationResult PublishShaderArtifactCacheRecord(
		const std::filesystem::path& binaryPath,
		const std::filesystem::path& recordPath,
		const ShaderArtifactCacheRecord& record) noexcept;
}
