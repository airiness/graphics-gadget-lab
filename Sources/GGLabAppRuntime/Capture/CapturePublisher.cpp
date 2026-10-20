#include "Capture/CapturePublisher.h"
#include "Capture/FrameCaptureMetadata.h"

#include <cstddef>
#include <exception>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace gglab
{
	const char* GetCaptureWriteStageName(CaptureWriteStage stage) noexcept
	{
		switch (stage)
		{
		case CaptureWriteStage::Queued:
			return "queued";
		case CaptureWriteStage::Encoding:
			return "encoding";
		case CaptureWriteStage::SerializingGraph:
			return "serializing graph";
		case CaptureWriteStage::WritingGraph:
			return "writing graph";
		case CaptureWriteStage::WritingImage:
			return "writing image";
		case CaptureWriteStage::WritingMetadata:
			return "writing metadata";
		case CaptureWriteStage::Publishing:
			return "publishing";
		case CaptureWriteStage::Done:
			return "done";
		}
		return "unknown";
	}

	bool CapturePublicationControl::TryAbandon() noexcept
	{
		Claim expected = Claim::Open;
		return m_Claim.compare_exchange_strong(expected, Claim::Abandoned);
	}

	bool CapturePublicationControl::TryClaim() noexcept
	{
		Claim expected = Claim::Open;
		return m_Claim.compare_exchange_strong(expected, Claim::Publishing);
	}

	bool CapturePublicationControl::IsAbandoned() const noexcept
	{
		return m_Claim == Claim::Abandoned;
	}

	CapturePublicationOutcome CapturePublicationControl::GetUnfinishedOutcome() const noexcept
	{
		return m_Claim == Claim::Publishing ?
			CapturePublicationOutcome::Indeterminate :
			CapturePublicationOutcome::NotPublished;
	}

	namespace
	{
		class NativeCapturePublicationFileSystem final : public CapturePublicationFileSystemBase
		{
		public:
			std::string Write(const std::filesystem::path& path, std::span<const uint8_t> bytes,
				std::span<const uint8_t> tail) noexcept override
			{
				std::error_code error;
				std::filesystem::create_directories(path.parent_path(), error);
				if (!std::filesystem::is_directory(path.parent_path(), error))
				{
					return std::format("The output directory '{}' could not be created.",
						GetFrameCapturePathText(path.parent_path()));
				}
				std::ofstream file(path, std::ios::binary | std::ios::trunc);
				file.write(reinterpret_cast<const char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
				if (!tail.empty())
				{
					file.write(reinterpret_cast<const char*>(tail.data()),
						static_cast<std::streamsize>(tail.size()));
				}
				file.close();
				if (!file)
				{
					std::filesystem::remove(path, error);
					return std::format("Writing '{}' failed.", GetFrameCapturePathText(path));
				}
				return {};
			}

			CaptureFilePublishOutcome Publish(const std::filesystem::path& temporary,
				const std::filesystem::path& final) noexcept override
			{
				std::error_code error;
				std::filesystem::create_hard_link(temporary, final, error);
				if (!error)
				{
					return CaptureFilePublishOutcome::Published;
				}
				if (error == std::errc::file_exists)
				{
					return CaptureFilePublishOutcome::NameTaken;
				}
				std::error_code existsError;
				if (std::filesystem::exists(final, existsError))
				{
					return CaptureFilePublishOutcome::NameTaken;
				}
				// Preserve the existing fallback on volumes without hard links. The
				// existence check and rename are not a filesystem-wide transaction.
				std::filesystem::rename(temporary, final, error);
				return error ? CaptureFilePublishOutcome::Failed : CaptureFilePublishOutcome::Published;
			}

			bool Withdraw(const std::filesystem::path& temporary,
				const std::filesystem::path& final) noexcept override
			{
				std::error_code error;
				if (std::filesystem::exists(temporary, error))
				{
					std::filesystem::remove(final, error);
				}
				else
				{
					std::filesystem::rename(final, temporary, error);
				}
				return !error;
			}

			void Remove(const std::filesystem::path& path) noexcept override
			{
				std::error_code error;
				std::filesystem::remove(path, error);
			}
		};

		struct TemporaryFiles
		{
			CapturePublicationFileSystemBase& m_Files;
			std::vector<std::filesystem::path> m_Paths;
			~TemporaryFiles()
			{
				for (const auto& path : m_Paths)
				{
					m_Files.Remove(path);
				}
			}
		};
	}

	std::shared_ptr<CapturePublicationFileSystemBase> CreateCapturePublicationFileSystem()
	{
		return std::make_shared<NativeCapturePublicationFileSystem>();
	}

	CapturePublicationReceipt PublishCaptureFiles(const CapturePublicationContent& content,
		const std::filesystem::path& directory, const std::string& baseStem,
		const std::string& temporaryStem, CapturePublicationControl& control,
		CapturePublicationFileSystemBase& files) noexcept
	{
		TemporaryFiles temporary{ files, {} };
		std::vector<std::filesystem::path> paths;
		size_t publishedCount = 0;
		const auto rollback = [&]() noexcept
			{
				bool complete = true;
				while (publishedCount != 0)
				{
					--publishedCount;
					complete = files.Withdraw(temporary.m_Paths[publishedCount],
						paths[publishedCount]) && complete;
				}
				return complete;
			};
		const auto failure = [&](std::string reason)
			{
				const bool withdrawn = rollback();
				return CapturePublicationReceipt{
					.m_Outcome = withdrawn ?
						CapturePublicationOutcome::NotPublished :
						CapturePublicationOutcome::Indeterminate,
					.m_Failure = withdrawn ?
						std::move(reason) :
						"Capture rollback failed; publication is indeterminate.",
				};
			};
		try
		{
			if (control.IsAbandoned())
			{
				return failure("The capture publication was abandoned.");
			}
			if (content.m_Files.empty())
			{
				return failure("The capture has no files to publish.");
			}
			for (const auto& file : content.m_Files)
			{
				temporary.m_Paths.push_back(directory / (temporaryStem + file.m_Suffix + ".partial"));
				paths.push_back(directory / (baseStem + file.m_Suffix));
			}
			// Validate path-bound metadata before producing any files (including
			// invalid UTF-8), without fixing it to names a collision may change.
			for (const auto& file : content.m_Files)
			{
				if (file.m_MakeBytes)
				{
					(void)file.m_MakeBytes(paths);
				}
			}
			if (content.m_MakeMarker)
			{
				(void)content.m_MakeMarker(paths);
			}
			for (size_t i = 0; i < content.m_Files.size(); ++i)
			{
				if (content.m_Files[i].m_MakeBytes ||
					(i + 1 == content.m_Files.size() && content.m_MakeMarker))
				{
					continue;
				}
				const auto& file = content.m_Files[i];
				control.m_Stage = file.m_Stage;
				const auto tail = file.m_Tail ?
					std::span<const uint8_t>(*file.m_Tail) :
					std::span<const uint8_t>{};
				std::string reason = files.Write(temporary.m_Paths[i], file.m_Bytes, tail);
				if (!reason.empty())
				{
					return failure(std::move(reason));
				}
			}
			if (!control.TryClaim())
			{
				return failure("The capture publication was abandoned.");
			}
			constexpr uint32_t MaxPublishAttempts = 1000;
			for (uint32_t attempt = 1; attempt <= MaxPublishAttempts; ++attempt)
			{
				const std::string stem = attempt == 1 ? baseStem : std::format("{}-{}", baseStem, attempt);
				for (size_t i = 0; i < paths.size(); ++i)
				{
					paths[i] = directory / (stem + content.m_Files[i].m_Suffix);
				}
				CaptureFilePublishOutcome outcome = CaptureFilePublishOutcome::Published;
				for (size_t i = 0; i < paths.size(); ++i)
				{
					const auto& makeBytes = content.m_Files[i].m_MakeBytes ?
						content.m_Files[i].m_MakeBytes :
						i + 1 == paths.size() ?
							content.m_MakeMarker :
							content.m_Files[i].m_MakeBytes;
					if (makeBytes)
					{
						control.m_Stage = CaptureWriteStage::WritingMetadata;
						const std::string json = makeBytes(paths);
						std::string reason = files.Write(temporary.m_Paths[i],
							{ reinterpret_cast<const uint8_t*>(json.data()), json.size() }, {});
						if (!reason.empty())
						{
							return failure(std::move(reason));
						}
					}
					control.m_Stage = CaptureWriteStage::Publishing;
					outcome = files.Publish(temporary.m_Paths[i], paths[i]);
					if (outcome != CaptureFilePublishOutcome::Published)
					{
						break;
					}
					++publishedCount;
				}
				if (outcome == CaptureFilePublishOutcome::Published)
				{
					return { .m_Outcome = CapturePublicationOutcome::Published, .m_Files = std::move(paths) };
				}
				if (outcome == CaptureFilePublishOutcome::Failed)
				{
					return failure("Publishing the capture file group failed.");
				}
				if (!rollback())
				{
					return { .m_Outcome = CapturePublicationOutcome::Indeterminate,
						.m_Failure = "Capture rollback failed; publication is indeterminate." };
				}
			}
			return failure("No unused file name was found for the capture.");
		}
		catch (const std::exception& error)
		{
			return failure(std::format("Serializing the capture export failed: {}", error.what()));
		}
	}
}
