#include "PublicationTransactionSelfTests.h"

#include "Artifact/PublicationTransaction.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <process.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace gglab
{
	namespace
	{
		uint32_t g_RemainingRenameFailures = 0;

		[[nodiscard]] bool InjectRenameFailure(
			const std::filesystem::path& destination) noexcept
		{
			(void)destination;
			if (g_RemainingRenameFailures > 0)
			{
				--g_RemainingRenameFailures;
				return true;
			}
			return false;
		}

		struct ObservationState
		{
			std::filesystem::path m_MarkerPath;
			uint32_t m_RemainingFailures = 0;
			uint32_t m_CallCount = 0;
			bool m_SawMarker = false;
		};

		[[nodiscard]] bool ObserveMarker(void* context) noexcept
		{
			auto* state = static_cast<ObservationState*>(context);
			++state->m_CallCount;
			if (state->m_RemainingFailures > 0)
			{
				--state->m_RemainingFailures;
				return false;
			}
			std::error_code errorCode;
			state->m_SawMarker = std::filesystem::exists(state->m_MarkerPath, errorCode);
			return true;
		}

		[[nodiscard]] std::span<const std::byte> AsBytes(const std::string& text) noexcept
		{
			return std::span(
				reinterpret_cast<const std::byte*>(text.data()), text.size());
		}

		[[nodiscard]] bool FileHasBytes(const std::filesystem::path& path,
			std::span<const std::byte> expectedBytes)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return false;
			}
			std::vector<std::byte> observed(expectedBytes.size());
			input.read(
				reinterpret_cast<char*>(observed.data()),
				static_cast<std::streamsize>(observed.size()));
			return input.gcount() == static_cast<std::streamsize>(observed.size()) &&
				input.peek() == std::char_traits<char>::eof() &&
				std::ranges::equal(observed, expectedBytes);
		}

		[[nodiscard]] bool HasTempResidue(const std::filesystem::path& directory) noexcept
		{
			std::error_code errorCode;
			for (const std::filesystem::directory_entry& entry :
				std::filesystem::directory_iterator(directory, errorCode))
			{
				if (entry.path().filename().wstring().find(L".tmp.") != std::wstring::npos)
				{
					return true;
				}
			}
			return false;
		}

		void ResetInjection() noexcept
		{
			g_RemainingRenameFailures = 0;
		}
	}

	void RunPublicationTransactionSelfTests(SelfTestContext& context) noexcept
	{
		std::error_code errorCode;
		const std::filesystem::path tempRoot =
			std::filesystem::temp_directory_path(errorCode) /
			(L"GGLabPublicationTransaction-" +
				std::to_wstring(static_cast<uint32_t>(::_getpid())));
		context.Check(!errorCode, "Publication transaction test resolves a temporary root");
		if (errorCode)
		{
			return;
		}
		std::filesystem::remove_all(tempRoot, errorCode);
		errorCode.clear();
		std::filesystem::create_directories(tempRoot, errorCode);
		context.Check(!errorCode, "Publication transaction test creates its temporary root");
		if (errorCode)
		{
			return;
		}

		const std::string primaryText = "published-primary-content";
		const std::string markerText = "published-marker-content";
		const std::span<const std::byte> primaryBytes = AsBytes(primaryText);
		const std::span<const std::byte> markerBytes = AsBytes(markerText);

		{
			// Byte-equality verification publishes both files and leaves no
			// temporary residue behind.
			const std::filesystem::path directory = tempRoot / "byte-equality";
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
					.m_Verification = PublicationVerification::ByteEquality,
				},
				PublicationFileSpec{
					.m_Destination = directory / "content.json",
					.m_Content = markerBytes,
					.m_Verification = PublicationVerification::ByteEquality,
				},
			};
			ObservationState observation{
				.m_MarkerPath = directory / "content.json",
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files,
				{
					.m_ObservationAttempts = 8,
					.m_ObservationIntervalMs = 0,
					.m_Observer = { .m_Context = &observation, .m_Observe = &ObserveMarker },
				});
			context.Check(result.m_Verified && result.m_PrimaryCommitted &&
				result.m_MarkerCommitted && result.m_Observed && observation.m_SawMarker &&
				FileHasBytes(directory / "content.bin", primaryBytes) &&
				FileHasBytes(directory / "content.json", markerBytes) &&
				!HasTempResidue(directory),
				"Publication transaction publishes verified content without temporary residue");
		}

		{
			// A digest mismatch fails verification before any commit.
			const std::filesystem::path directory = tempRoot / "digest-mismatch";
			Sha256Digest wrongDigest{};
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
					.m_Verification = PublicationVerification::ContentDigest,
					.m_ExpectedDigest = wrongDigest,
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(files, {});
			context.Check(!result.m_Verified && !result.m_PrimaryCommitted &&
				!std::filesystem::exists(directory / "content.bin", errorCode) &&
				!HasTempResidue(directory),
				"Publication transaction rejects content that fails digest verification");
		}

		{
			// A matching digest is accepted as the verification policy.
			const std::filesystem::path directory = tempRoot / "digest-match";
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
					.m_Verification = PublicationVerification::ContentDigest,
					.m_ExpectedDigest = ComputeSha256(primaryBytes),
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(files, {});
			context.Check(result.m_Verified && result.m_PrimaryCommitted &&
				FileHasBytes(directory / "content.bin", primaryBytes),
				"Publication transaction publishes content that matches its digest");
		}

		{
			// ReplaceExisting replaces a pre-existing destination.
			const std::filesystem::path directory = tempRoot / "replace-existing";
			const std::filesystem::path destination = directory / "content.bin";
			std::filesystem::create_directories(directory, errorCode);
			{
				std::ofstream output(destination, std::ios::binary);
				const std::string staleText = "stale-content";
				output.write(staleText.data(),
					static_cast<std::streamsize>(staleText.size()));
			}
			const std::array files{
				PublicationFileSpec{
					.m_Destination = destination,
					.m_Content = primaryBytes,
					.m_Verification = PublicationVerification::ByteEquality,
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files, { .m_Commit = PublicationCommit::ReplaceExisting });
			context.Check(result.m_PrimaryCommitted &&
				FileHasBytes(destination, primaryBytes),
				"Publication transaction replaces an existing destination in replace mode");
		}

		{
			// Exclusive rename failure without recovery leaves the marker
			// unpublished.
			const std::filesystem::path directory = tempRoot / "exclusive-failure";
			ResetInjection();
			g_RemainingRenameFailures = 1;
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
				},
				PublicationFileSpec{
					.m_Destination = directory / "content.json",
					.m_Content = markerBytes,
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files, { .m_RenameFailureInjector = &InjectRenameFailure });
			ResetInjection();
			context.Check(!result.m_PrimaryCommitted && !result.m_MarkerCommitted &&
				!HasTempResidue(directory),
				"Publication transaction treats an injected rename failure as not committed");
		}

		{
			// Orphan recovery removes an unmarked primary destination and
			// retries its rename.
			const std::filesystem::path directory = tempRoot / "orphan-recovery";
			const std::filesystem::path primaryDestination = directory / "content.bin";
			std::filesystem::create_directories(directory, errorCode);
			{
				std::ofstream output(primaryDestination, std::ios::binary);
				const std::string orphanText = "orphaned-content";
				output.write(orphanText.data(),
					static_cast<std::streamsize>(orphanText.size()));
			}
			ResetInjection();
			g_RemainingRenameFailures = 1;
			const std::array files{
				PublicationFileSpec{
					.m_Destination = primaryDestination,
					.m_Content = primaryBytes,
				},
				PublicationFileSpec{
					.m_Destination = directory / "content.json",
					.m_Content = markerBytes,
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files,
				{
					.m_RecoverOrphanedPrimary = true,
					.m_RenameFailureInjector = &InjectRenameFailure,
				});
			ResetInjection();
			context.Check(result.m_PrimaryCommitted && result.m_MarkerCommitted &&
				FileHasBytes(primaryDestination, primaryBytes) &&
				FileHasBytes(directory / "content.json", markerBytes),
				"Publication transaction recovers an orphaned primary destination");
		}

		{
			// ContinueWhenPrimaryPresent commits the marker when another
			// producer's primary already occupies the slot.
			const std::filesystem::path directory = tempRoot / "primary-present";
			std::filesystem::create_directories(directory, errorCode);
			{
				std::ofstream output(directory / "content.bin", std::ios::binary);
				const std::string presentText = "concurrent-content";
				output.write(presentText.data(),
					static_cast<std::streamsize>(presentText.size()));
			}
			ResetInjection();
			g_RemainingRenameFailures = 1;
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
				},
				PublicationFileSpec{
					.m_Destination = directory / "content.json",
					.m_Content = markerBytes,
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files,
				{
					.m_ContinueWhenPrimaryPresent = true,
					.m_RenameFailureInjector = &InjectRenameFailure,
				});
			ResetInjection();
			context.Check(!result.m_PrimaryCommitted && result.m_MarkerCommitted &&
				FileHasBytes(directory / "content.json", markerBytes),
				"Publication transaction commits the marker when the primary already exists");
		}

		{
			// Observation retries within its bounded window.
			const std::filesystem::path directory = tempRoot / "observation-window";
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
				},
			};
			ObservationState observation{
				.m_MarkerPath = directory / "content.bin",
				.m_RemainingFailures = 2,
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files,
				{
					.m_ObservationAttempts = 8,
					.m_ObservationIntervalMs = 0,
					.m_Observer = { .m_Context = &observation, .m_Observe = &ObserveMarker },
				});
			context.Check(result.m_Observed && observation.m_CallCount == 3 &&
				observation.m_SawMarker,
				"Publication transaction retries observation within its bounded window");
		}

		{
			// A failed observation with repair removes both destinations and
			// retries the transaction.
			const std::filesystem::path directory = tempRoot / "observation-repair";
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
				},
				PublicationFileSpec{
					.m_Destination = directory / "content.json",
					.m_Content = markerBytes,
				},
			};
			ObservationState observation{
				.m_MarkerPath = directory / "content.json",
				.m_RemainingFailures = 64,
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(
				files,
				{
					.m_MaxAttempts = 2,
					.m_RemoveDestinationsOnFailedObservation = true,
					.m_ObservationAttempts = 1,
					.m_ObservationIntervalMs = 0,
					.m_Observer = { .m_Context = &observation, .m_Observe = &ObserveMarker },
				});
			context.Check(!result.m_Observed &&
				!std::filesystem::exists(directory / "content.bin", errorCode) &&
				!std::filesystem::exists(directory / "content.json", errorCode) &&
				!HasTempResidue(directory),
				"Publication transaction repairs failed observations and retries");
		}

		{
			// Without an observer the transaction returns after committing.
			const std::filesystem::path directory = tempRoot / "no-observer";
			const std::array files{
				PublicationFileSpec{
					.m_Destination = directory / "content.bin",
					.m_Content = primaryBytes,
				},
			};
			const PublicationTransactionResult result = ExecutePublicationTransaction(files, {});
			context.Check(result.m_PrimaryCommitted && !result.m_Observed &&
				FileHasBytes(directory / "content.bin", primaryBytes),
				"Publication transaction commits without an observation callback");
		}

		std::filesystem::remove_all(tempRoot, errorCode);
	}
}
