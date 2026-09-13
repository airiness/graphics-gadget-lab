#include "Artifact/PublicationTransaction.h"

#include "GGLabFoundation/IO/PathUtils.h"

#include <process.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::filesystem::path MakeUniqueTempPath(
			const std::filesystem::path& destination) noexcept
		{
			static std::atomic_uint64_t counter = 0;
			return destination.wstring() + L".tmp." +
				std::to_wstring(static_cast<uint32_t>(::_getpid())) + L"." +
				std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
		}

		void RemoveFileBestEffort(const std::filesystem::path& path) noexcept
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		[[nodiscard]] bool HasFile(const std::filesystem::path& path) noexcept
		{
			std::error_code errorCode;
			return std::filesystem::is_regular_file(path, errorCode);
		}

		[[nodiscard]] bool FileEquals(const std::filesystem::path& path,
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

		[[nodiscard]] std::optional<Sha256Digest> ComputeFileDigest(
			const std::filesystem::path& path) noexcept
		{
			std::error_code errorCode;
			const auto fileSize = std::filesystem::file_size(path, errorCode);
			if (errorCode)
			{
				return std::nullopt;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return std::nullopt;
			}
			std::vector<std::byte> bytes(static_cast<size_t>(fileSize));
			input.read(
				reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size()))
			{
				return std::nullopt;
			}
			return ComputeSha256(bytes);
		}

		[[nodiscard]] bool CommitFile(const std::filesystem::path& source,
			const std::filesystem::path& destination, PublicationCommit mode,
			PublicationRenameFailureFn injector) noexcept
		{
			if (injector != nullptr && injector(destination))
			{
				return false;
			}
			if (mode == PublicationCommit::ReplaceExisting)
			{
				return ::MoveFileExW(source.c_str(), destination.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			std::error_code errorCode;
			std::filesystem::rename(source, destination, errorCode);
			return !errorCode;
		}
	}

	PublicationTransactionResult ExecutePublicationTransaction(
		std::span<const PublicationFileSpec> files,
		const PublicationTransactionConfig& config) noexcept
	{
		PublicationTransactionResult result{};
		if (files.empty() || files.front().m_Destination.empty() ||
			files.back().m_Destination.empty())
		{
			return result;
		}
		const std::filesystem::path primaryDestination = files.front().m_Destination;
		const std::filesystem::path markerDestination = files.back().m_Destination;

		for (uint32_t attempt = 0; attempt < config.m_MaxAttempts; ++attempt)
		{
			std::vector<std::filesystem::path> tempPaths;
			tempPaths.reserve(files.size());
			for (const PublicationFileSpec& file : files)
			{
				if (!utils::CreateParentDirectoryIfNotExist(file.m_Destination))
				{
					return result;
				}
				tempPaths.push_back(MakeUniqueTempPath(file.m_Destination));
			}

			bool written = true;
			for (size_t index = 0; index < files.size(); ++index)
			{
				if (!utils::WriteFileBinary(tempPaths[index], files[index].m_Content))
				{
					written = false;
					break;
				}
			}
			if (!written)
			{
				for (const std::filesystem::path& tempPath : tempPaths)
				{
					RemoveFileBestEffort(tempPath);
				}
				return result;
			}
			result.m_TempsWritten = true;

			bool verified = true;
			for (size_t index = 0; index < files.size(); ++index)
			{
				switch (files[index].m_Verification)
				{
				case PublicationVerification::None:
					break;
				case PublicationVerification::ByteEquality:
					verified = FileEquals(tempPaths[index], files[index].m_Content);
					break;
				case PublicationVerification::ContentDigest:
				{
					const std::optional<Sha256Digest> digest =
						ComputeFileDigest(tempPaths[index]);
					verified = digest.has_value() &&
						*digest == files[index].m_ExpectedDigest;
					break;
				}
				}
				if (!verified)
				{
					break;
				}
			}
			if (!verified)
			{
				for (const std::filesystem::path& tempPath : tempPaths)
				{
					RemoveFileBestEffort(tempPath);
				}
				return result;
			}
			result.m_Verified = true;

			result.m_PrimaryCommitted = CommitFile(tempPaths.front(),
				primaryDestination, config.m_Commit, config.m_RenameFailureInjector);
			if (!result.m_PrimaryCommitted && config.m_RecoverOrphanedPrimary &&
				!HasFile(markerDestination))
			{
				// Orphaned primary (no commit marker): derived data is safe to
				// discard, so recover by removing it and retrying once.
				RemoveFileBestEffort(primaryDestination);
				result.m_PrimaryCommitted = CommitFile(tempPaths.front(),
					primaryDestination, config.m_Commit, config.m_RenameFailureInjector);
			}

			if (files.size() > 1)
			{
				const bool primaryPresent =
					result.m_PrimaryCommitted || HasFile(primaryDestination);
				const bool commitMarker = config.m_ContinueWhenPrimaryPresent
					? primaryPresent
					: result.m_PrimaryCommitted;
				if (commitMarker)
				{
					result.m_MarkerCommitted = CommitFile(tempPaths.back(),
						markerDestination, config.m_Commit,
						config.m_RenameFailureInjector);
				}
			}
			else
			{
				result.m_MarkerCommitted = result.m_PrimaryCommitted;
			}

			for (const std::filesystem::path& tempPath : tempPaths)
			{
				RemoveFileBestEffort(tempPath);
			}

			if (config.m_Observer.m_Observe != nullptr)
			{
				for (uint32_t observation = 0; observation < config.m_ObservationAttempts;
					++observation)
				{
					if (config.m_Observer.m_Observe(config.m_Observer.m_Context))
					{
						result.m_Observed = true;
						break;
					}
					if (config.m_ObservationIntervalMs > 0 &&
						observation + 1 < config.m_ObservationAttempts)
					{
						std::this_thread::sleep_for(
							std::chrono::milliseconds(config.m_ObservationIntervalMs));
					}
				}
			}

			if (result.m_Observed || !config.m_RemoveDestinationsOnFailedObservation)
			{
				return result;
			}
			RemoveFileBestEffort(markerDestination);
			RemoveFileBestEffort(primaryDestination);
		}
		return result;
	}
}
