#pragma once
#include "GGLabFoundation/Hash/Sha256.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace gglab
{
	// Shader Toolchain internal publication mechanics. Only ShaderToolchainCore
	// publication paths include this header; it is not a consumer contract.
	enum class PublicationVerification : uint8_t
	{
		None,
		ByteEquality,
		ContentDigest,
	};

	enum class PublicationCommit : uint8_t
	{
		// Plain rename: an existing destination is never replaced by this mode.
		ExclusiveRename,
		// MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH.
		ReplaceExisting,
	};

	struct PublicationFileSpec
	{
		std::filesystem::path m_Destination;
		std::span<const std::byte> m_Content{};
		PublicationVerification m_Verification = PublicationVerification::ByteEquality;
		Sha256Digest m_ExpectedDigest{};
	};

	// Caller-owned observation of the committed slot. Returns true when the
	// entry is visible and structurally valid for the caller.
	struct PublicationObserver
	{
		void* m_Context = nullptr;
		bool (*m_Observe)(void* context) noexcept = nullptr;
	};

	// Injectable rename failure for tests; null disables injection.
	using PublicationRenameFailureFn = bool (*)(
		const std::filesystem::path& destination) noexcept;

	struct PublicationTransactionConfig
	{
		PublicationCommit m_Commit = PublicationCommit::ExclusiveRename;
		// Remove an orphaned primary destination (same path, no commit marker)
		// and retry its rename exactly once.
		bool m_RecoverOrphanedPrimary = false;
		// Attempt the commit marker even when the primary rename failed but a
		// primary destination already exists.
		bool m_ContinueWhenPrimaryPresent = false;
		uint32_t m_MaxAttempts = 1;
		// Derived-data repair: remove both destinations after a failed
		// observation and retry the transaction while attempts remain.
		bool m_RemoveDestinationsOnFailedObservation = false;
		uint32_t m_ObservationAttempts = 1;
		uint32_t m_ObservationIntervalMs = 1;
		PublicationRenameFailureFn m_RenameFailureInjector = nullptr;
		PublicationObserver m_Observer{};
	};

	struct PublicationTransactionResult
	{
		bool m_TempsWritten = false;
		bool m_Verified = false;
		bool m_PrimaryCommitted = false;
		bool m_MarkerCommitted = false;
		bool m_Observed = false;
	};

	// Executes one ordered write/verify/commit/observe transaction. files[0] is
	// the primary content and the last entry is the commit marker.
	[[nodiscard]] PublicationTransactionResult ExecutePublicationTransaction(
		std::span<const PublicationFileSpec> files,
		const PublicationTransactionConfig& config) noexcept;
}
