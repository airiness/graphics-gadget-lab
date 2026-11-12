#pragma once

#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/Capture/ResourceExportTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	class FakeCaptureControl final : public FrameCaptureControlBase
	{
	public:
		struct Issued
		{
			uint64_t m_Id = 0;
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
			uint64_t m_FrameSerial = 0;
			std::optional<PostProcessDebugTap> m_DiagnosticTap;
			std::vector<RGResourceAddress> m_Targets;
			bool m_IncludeGraph = false;
			bool m_Defer = false;
			uint64_t m_GroupId = 0;
		};

		uint64_t RequestCapture(FrameCaptureSource source, uint64_t frameSerial = 0, uint64_t groupId = 0) noexcept override
		{
			if (m_RejectRequest || m_Issued.size() >= m_RejectAfterIssued ||
				GetRequestAvailability() != FrameCaptureAvailability::Available)
			{
				return 0;
			}
			m_Issued.push_back({ .m_Id = m_NextId, .m_Source = source, .m_FrameSerial = frameSerial, .m_GroupId = groupId });
			m_LiveRequests.push_back(m_NextId);
			return m_NextId++;
		}
		uint64_t RequestDiagnosticCapture(PostProcessDebugTap tap, uint64_t frameSerial = 0, uint64_t groupId = 0) noexcept override
		{
			if (m_RejectRequest || m_Issued.size() >= m_RejectAfterIssued ||
				GetRequestAvailability() != FrameCaptureAvailability::Available)
			{
				return 0;
			}
			m_Issued.push_back({ .m_Id = m_NextId, .m_Source = FrameCaptureSource::Diagnostic,
				.m_FrameSerial = frameSerial,
				.m_DiagnosticTap = tap, .m_GroupId = groupId });
			m_LiveRequests.push_back(m_NextId);
			return m_NextId++;
		}
		uint64_t RequestResourceExport(std::vector<RGResourceAddress> targets, uint64_t frameSerial,
			bool defer, bool includeGraph, uint64_t groupId = 0) noexcept override
		{
			const auto id = RequestCapture(FrameCaptureSource::ResourceExport, frameSerial, groupId);
			if (id != 0)
			{
				m_Issued.back().m_Targets = std::move(targets);
				m_Issued.back().m_Defer = defer;
				m_Issued.back().m_IncludeGraph = includeGraph;
			}
			return id;
		}
		void Busy(uint64_t id) noexcept
		{
			const auto issued = std::ranges::find(m_Issued, id, &Issued::m_Id);
			if (issued != m_Issued.end())
			{
				m_Results.push_back({ .m_RequestId = id, .m_Source = issued->m_Source,
					.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = issued->m_FrameSerial, .m_ResourceFailure = RGObservationFailure::Busy });
			}
		}
		void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept override
		{
			for (const auto& result : m_Results)
			{
				std::erase(m_LiveRequests, result.m_RequestId);
			}
			outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()), std::make_move_iterator(m_Results.end()));
			m_Results.clear();
		}
		bool HasNonFiniteInstrumentation() const noexcept override { return m_Instrumentation; }
		void ConsumeNonFiniteProductionFrames(std::vector<std::shared_ptr<const NonFiniteProductionFrame>>& frames) noexcept override
		{
			frames.insert(frames.end(), std::make_move_iterator(m_Production.begin()), std::make_move_iterator(m_Production.end()));
			m_Production.clear();
		}
		uint32_t GetUnfinishedRequestCount() const noexcept override { return static_cast<uint32_t>(m_LiveRequests.size()); }
		std::optional<PostProcessDebugTap> GetPendingDiagnosticTap() const noexcept override
		{
			return m_PendingDiagnosticTap;
		}
		FrameCaptureAvailability GetResourceExportFrameAvailability() const noexcept override
		{
			return m_ResourceFrameAvailability == FrameCaptureAvailability::Available ? m_RequestAvailability : m_ResourceFrameAvailability;
		}
		FrameCaptureAvailability GetRequestAvailability(uint32_t requestCount = 1) const noexcept override
		{
			m_QueriedRequestCount = requestCount;
			if (m_RequestAvailability != FrameCaptureAvailability::Available)
			{
				return m_RequestAvailability;
			}
			if (requestCount > m_RequestLimit)
			{
				return FrameCaptureAvailability::TooLarge;
			}
			return m_LiveRequests.size() > m_RequestLimit - requestCount ? FrameCaptureAvailability::Busy : FrameCaptureAvailability::Available;
		}
		FrameCaptureAvailability GetAvailability(RHIFormat format, uint32_t width, uint32_t height) const noexcept override
		{
			m_QueriedFormat = format;
			m_QueriedExtent = { width, height };
			return m_Availability;
		}

		void Complete(uint64_t id, std::optional<uint64_t> frameSerial = std::nullopt) noexcept
		{
			const auto issued = std::ranges::find(m_Issued, id, &Issued::m_Id);
			if (issued == m_Issued.end())
			{
				return;
			}
			auto image = std::make_shared<FrameCaptureImage>();
			image->m_Format = RHIFormat::B8G8R8A8Unorm;
			image->m_Width = 2;
			image->m_Height = 2;
			image->m_Pixels = { 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 10, 20, 30, 0 };
			m_LastImage = image;
			std::shared_ptr<RGSnapshot> graph;
			if (!m_OmitGraph && (issued->m_Source == FrameCaptureSource::RenderGraph || issued->m_IncludeGraph))
			{
				graph = std::make_shared<RGSnapshot>();
				graph->m_Execution.m_Recorded = true;
			}
			std::shared_ptr<ResourceExportPayload> resources;
			if (issued->m_Source == FrameCaptureSource::ResourceExport)
			{
				resources = std::make_shared<ResourceExportPayload>();
				for (const auto& target : issued->m_Targets)
				{
					ResourceExportArray array{ .m_Address = target, .m_Scalar = ResourceExportScalar::Float32,
						.m_Shape = { 1, 1, 2, 1 } };
					const float values[] = { 1.25f, -2.0f };
					array.m_Data.resize(sizeof(values));
					std::memcpy(array.m_Data.data(), values, sizeof(values));
					resources->m_Arrays.push_back(std::move(array));
				}
			}
			m_Results.push_back({
				.m_RequestId = id,
				.m_Source = issued->m_Source,
				.m_Status = FrameCaptureStatus::Completed,
				.m_FrameSerial = frameSerial.value_or(issued->m_FrameSerial),
				.m_DiagnosticTap = issued->m_DiagnosticTap,
				.m_Image = graph || resources ? nullptr : std::move(image),
				.m_Graph = graph,
				.m_Resources = resources,
				.m_ResourceExportActive = bool(resources),
				.m_SubmissionFenceIndex = 3,
				.m_SubmissionFenceGeneration = 1,
				.m_SubmissionFenceValue = 19,
				.m_AdapterCompatibilityIdentity = "Capture.TestAdapter",
			});
		}
		void Fail(uint64_t id, std::string failure) noexcept
		{
			m_Results.push_back({
				.m_RequestId = id,
				.m_Status = FrameCaptureStatus::Failed,
				.m_Failure = std::move(failure),
			});
		}

		std::vector<Issued> m_Issued;
		std::weak_ptr<const FrameCaptureImage> m_LastImage;
		std::optional<PostProcessDebugTap> m_PendingDiagnosticTap;
		FrameCaptureAvailability m_Availability = FrameCaptureAvailability::Available;
		FrameCaptureAvailability m_RequestAvailability = FrameCaptureAvailability::Available;
		FrameCaptureAvailability m_ResourceFrameAvailability = FrameCaptureAvailability::Available;
		bool m_RejectRequest = false;
		size_t m_RejectAfterIssued = std::numeric_limits<size_t>::max();
		bool m_OmitGraph = false;
		bool m_Instrumentation = false;
		std::vector<std::shared_ptr<const NonFiniteProductionFrame>> m_Production;
		uint32_t m_RequestLimit = 64;
		mutable RHIFormat m_QueriedFormat = RHIFormat::Unknown;
		mutable uint32_t m_QueriedRequestCount = 0;
		mutable std::array<uint32_t, 2> m_QueriedExtent{};

	private:
		std::vector<uint64_t> m_LiveRequests;
		std::vector<FrameCaptureResult> m_Results;
		uint64_t m_NextId = 100;
	};

	class TemporaryDirectory final
	{
	public:
		explicit TemporaryDirectory(std::string_view name) noexcept
		{
			static std::atomic<uint32_t> sequence = 0;
			std::error_code errorCode;
			m_Path = std::filesystem::temp_directory_path(errorCode) /
				std::format("gglab-{}-{}-{}", name, std::chrono::steady_clock::now().time_since_epoch().count(), sequence.fetch_add(1));
		}
		TemporaryDirectory(const TemporaryDirectory&) = delete;
		TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
		~TemporaryDirectory() noexcept
		{
			std::error_code errorCode;
			std::filesystem::remove_all(m_Path, errorCode);
		}

		[[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};

	[[nodiscard]] inline FrameCaptureFrameState MakeFrameState(
		bool ready, uint64_t temporalSession = 1, std::string labId = "gglab.lab.test")
	{
		FrameCaptureFrameState state{
			.m_Backend = "dx12",
			.m_DemoId = "Demo.LabHost",
			.m_LabId = std::move(labId),
			.m_SettleKey = { .m_Temporal = {
				.m_Identity = { .m_DisplayViewId = RenderViewID::Main,
					.m_SessionIdentity = temporalSession,
					.m_ColorExtent = { 64, 32 }, .m_DepthExtent = { 64, 32 } },
				.m_Services = TemporalService::FrameContinuity | TemporalService::ColorDepthHistory,
				.m_HasCompatiblePreviousHistory = true } },
			.m_DisplayFormat = RHIFormat::B8G8R8A8Unorm,
			.m_FrameSerial = 17,
			.m_FrameIndex = 42,
			.m_Camera = { .m_Name = "Main", .m_Position = { 1.0f, 2.0f, 3.0f },
				.m_VerticalFovDegrees = 60.0f },
			.m_FixedDeltaTime = 1.0 / 60.0,
			.m_TotalTime = 0.5,
		};
		state.m_Readiness.Add("shaders", FrameCaptureGateState::Ready);
		state.m_Readiness.Add("lab", ready ? FrameCaptureGateState::Ready : FrameCaptureGateState::Pending, ready ? "" : "Warming up.");
		return state;
	}

	// A test client owns only the IDs it admitted and consumes real job receipts.
	// It never introduces a second request/result representation or bypasses admission.
	class CaptureTestJobs final
	{
	public:
		CaptureTestJobs(FrameCaptureCoordinator& coordinator, SelfTestContext& context) noexcept :
			m_Coordinator(coordinator), m_Context(context) {}

		[[nodiscard]] uint64_t Admit(CapturePlan plan) noexcept
		{
			const CaptureAdmission admission = m_Coordinator.TrySubmit(std::move(plan));
			m_Context.Check(admission.m_Status == CaptureAdmissionStatus::Accepted,
				"The test client admits its typed job through the bounded public contract");
			if (admission.m_Status == CaptureAdmissionStatus::Accepted)
			{
				m_Pending.push_back(admission.m_JobId);
			}
			return admission.m_JobId;
		}

		[[nodiscard]] std::vector<CaptureJobResult> TakeResults() noexcept
		{
			std::vector<CaptureJobResult> results;
			std::erase_if(m_Pending, [&](uint64_t id)
				{
					auto result = m_Coordinator.TakeJobResult(id);
					if (!result)
					{
						return false;
					}
					results.push_back(std::move(*result));
					return true;
				});
			return results;
		}

	private:
		FrameCaptureCoordinator& m_Coordinator;
		SelfTestContext& m_Context;
		std::vector<uint64_t> m_Pending;
	};

	[[nodiscard]] inline std::vector<std::filesystem::path> ProductFiles(
		const CaptureJobResult& result, std::string_view extension,
		std::optional<CaptureProductKind> kind = std::nullopt)
	{
		std::vector<std::filesystem::path> files;
		for (const CaptureProductResult& product : result.m_Products)
		{
			if (kind && product.m_Kind != *kind)
			{
				continue;
			}
			for (const auto& path : product.m_Files)
			{
				if (path.extension() == extension)
				{
					files.push_back(path);
				}
			}
		}
		return files;
	}

	[[nodiscard]] inline std::filesystem::path ProductFile(const CaptureJobResult& result,
		std::string_view extension, std::optional<CaptureProductKind> kind = std::nullopt)
	{
		const auto files = ProductFiles(result, extension, kind);
		return files.empty() ? std::filesystem::path{} : files.front();
	}

	[[nodiscard]] inline std::string ReadText(const std::filesystem::path& path) noexcept
	{
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	// Stands in for the host's PNG encoder: a PNG signature followed by the
	// image bytes is enough for the publication contracts tested here.
	[[nodiscard]] inline std::optional<std::vector<uint8_t>> EncodeTestPng(const FrameCaptureImage& image) noexcept
	{
		std::vector<uint8_t> bytes{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		bytes.insert(bytes.end(), image.m_Pixels.begin(), image.m_Pixels.end());
		return bytes;
	}

	[[nodiscard]] inline bool HasPartialFile(const std::filesystem::path& directory) noexcept
	{
		std::error_code errorCode;
		for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
		{
			if (entry.path().extension() == ".partial")
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] inline bool HasPngSignature(const std::filesystem::path& path) noexcept
	{
		constexpr std::array<char, 8> signature{ '\x89', 'P', 'N', 'G', '\r', '\n', '\x1A',
			'\n' };
		const std::string bytes = ReadText(path);
		return bytes.size() > signature.size() &&
			std::equal(signature.begin(), signature.end(), bytes.begin());
	}

	[[nodiscard]] inline FrameCaptureFrameState MakeViewFrameState(uint64_t cameraResetSerial)
	{
		FrameCaptureFrameState state = MakeFrameState(true);
		state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity = cameraResetSerial;
		state.m_ReferenceViewIds = { "CAM_A", "CAM_B" };
		return state;
	}

	[[nodiscard]] inline bool HasAnyFile(const std::filesystem::path& directory) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(directory, errorCode))
		{
			return false;
		}
		for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
		{
			if (entry.is_regular_file(errorCode))
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] inline CapturePlan MakeRequiredTestPlan()
	{
		return {
			.m_RequiredProducts = { ImageCaptureProduct{},
				ResourceCaptureProduct{ { { .m_Resource = "A" } } }, GraphCaptureProduct{} },
			.m_Label = "required-products",
		};
	}
}
