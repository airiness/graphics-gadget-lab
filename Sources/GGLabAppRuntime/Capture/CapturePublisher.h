#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	enum class CaptureWriteStage : uint8_t
	{
		Queued,
		Encoding,
		SerializingGraph,
		WritingGraph,
		WritingImage,
		WritingMetadata,
		Publishing,
		Done,
	};
	[[nodiscard]] const char* GetCaptureWriteStageName(CaptureWriteStage stage) noexcept;

	enum class CapturePublicationOutcome : uint8_t
	{
		NotPublished,
		Published,
		Indeterminate,
	};

	// Shutdown may withdraw delivery only before publication is claimed. The claim
	// is independent of worker completion and does not imply successful publication.
	class CapturePublicationControl
	{
	public:
		[[nodiscard]] bool TryAbandon() noexcept;
		[[nodiscard]] bool TryClaim() noexcept;
		[[nodiscard]] bool IsAbandoned() const noexcept;
		[[nodiscard]] CapturePublicationOutcome GetUnfinishedOutcome() const noexcept;
		std::atomic<CaptureWriteStage> m_Stage = CaptureWriteStage::Queued;

	private:
		enum class Claim : uint8_t { Open, Publishing, Abandoned };
		std::atomic<Claim> m_Claim = Claim::Open;
	};

	enum class CaptureFilePublishOutcome : uint8_t { Published, NameTaken, Failed };

	// A narrow file-I/O seam for deterministic publication faults. Detached writers
	// retain this capability; implementations must not borrow the coordinator.
	class CapturePublicationFileSystemBase
	{
	public:
		virtual ~CapturePublicationFileSystemBase() = default;
		[[nodiscard]] virtual std::string Write(const std::filesystem::path& path,
			std::span<const uint8_t> bytes, std::span<const uint8_t> tail) noexcept = 0;
		[[nodiscard]] virtual CaptureFilePublishOutcome Publish(
			const std::filesystem::path& temporary, const std::filesystem::path& final) noexcept = 0;
		// Withdraw only a file created by this attempt, restoring renamed temporary
		// storage when necessary so a collision can retry the whole group.
		[[nodiscard]] virtual bool Withdraw(
			const std::filesystem::path& temporary, const std::filesystem::path& final) noexcept = 0;
		virtual void Remove(const std::filesystem::path& path) noexcept = 0;
	};
	[[nodiscard]] std::shared_ptr<CapturePublicationFileSystemBase>
		CreateCapturePublicationFileSystem();

	struct CapturePublicationFile
	{
		std::string m_Suffix;
		std::vector<uint8_t> m_Bytes;
		// An aliasing shared pointer retains the immutable product and its lease;
		// resource arrays are streamed without another full-size CPU allocation.
		std::shared_ptr<const std::vector<uint8_t>> m_Tail;
		CaptureWriteStage m_Stage = CaptureWriteStage::WritingGraph;
		// Bundle sidecars also depend on the final collision candidate's names.
		std::function<std::string(std::span<const std::filesystem::path>)> m_MakeBytes;
	};

	struct CapturePublicationContent
	{
		// The last file is the completion marker for this publication group.
		std::vector<CapturePublicationFile> m_Files;
		// Name-bound sidecars are validated before I/O and regenerated for each
		// collision candidate. Other product encoding is already complete.
		std::function<std::string(std::span<const std::filesystem::path>)> m_MakeMarker;
	};

	struct CapturePublicationReceipt
	{
		CapturePublicationOutcome m_Outcome = CapturePublicationOutcome::NotPublished;
		std::vector<std::filesystem::path> m_Files;
		std::string m_Failure;
	};

	// Owns temporary files, collision retries, rollback and the completion marker.
	// It has no knowledge of PNG, NPY, graph schemas, GPU work or watch claims.
	[[nodiscard]] CapturePublicationReceipt PublishCaptureFiles(
		const CapturePublicationContent& content, const std::filesystem::path& directory,
		const std::string& baseStem, const std::string& temporaryStem,
		CapturePublicationControl& control, CapturePublicationFileSystemBase& files) noexcept;
}
