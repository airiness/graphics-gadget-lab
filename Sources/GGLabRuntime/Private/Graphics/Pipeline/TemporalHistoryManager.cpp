#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		constexpr std::string_view TemporalHistoryNamePrefix = "TAA.History";

		[[nodiscard]] RHIOwnedTextureCreateInfo MakeHistoryTextureCreateInfo(
			RHIFormat format, uint32_t width, uint32_t height) noexcept
		{
			return {
				.m_Desc = {
					.m_Format = format,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
					.m_Extent = { width, height, 1 },
				},
				.m_InitialState = UndefinedRHITextureState(),
			};
		}
	}

	TemporalHistoryFormatSupport QueryTemporalHistoryFormatSupport(
		const RHIDevice& device) noexcept
	{
		const auto querySurface = [&device](RHIFormat format)
		{
			RHITextureDesc textureDesc{};
			textureDesc.m_Dimension = RHITextureDimension::Texture2D;
			textureDesc.m_Format = format;
			textureDesc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess;
			textureDesc.m_Extent = { 1, 1, 1 };
			RHITextureViewDesc viewDesc = MakeRHITexture2DViewDesc(format);
			viewDesc.m_Type = RHITextureViewType::ShaderResource;
			const RHITextureSupportResult shaderResource =
				device.QueryTextureViewSupport(textureDesc, viewDesc);
			viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
			return TemporalHistorySurfaceFormatSupport{
				.m_ShaderResource = shaderResource,
				.m_TypedUavStore = device.QueryTextureViewSupport(textureDesc, viewDesc),
			};
		};

		return {
			.m_Color = querySurface(TemporalHistoryColorFormat),
			.m_Depth = querySurface(TemporalHistoryDepthFormat),
			.m_Reliability = querySurface(TemporalHistoryReliabilityFormat),
		};
	}

	TemporalHistoryManager::TemporalHistoryManager(
		PersistentTexturePool* texturePool) noexcept : m_TexturePool(texturePool)
	{
		GGLAB_ASSERT_MSG(
			m_TexturePool != nullptr, "TemporalHistoryManager requires a persistent texture pool.");
	}

	TemporalHistoryManager::~TemporalHistoryManager() noexcept
	{
		GGLAB_ASSERT_MSG(m_Shutdown,
			"TemporalHistoryManager must be shut down after the RHI device becomes idle.");
		GGLAB_ASSERT_MSG(!m_ActiveHistory.has_value(),
			"TemporalHistoryManager destroyed with an active history set.");
	}

	TemporalHistoryFrameState TemporalHistoryManager::BeginFrame(
		const ResolvedTemporalFramePlan& plan, const ViewResolution& resolution,
		TemporalColorAbi colorAbi) noexcept
	{
		GGLAB_ASSERT_MSG(!m_Shutdown, "Temporal history cannot begin after shutdown.");
		if (m_Shutdown)
		{
			return {};
		}

		if (!plan.HasService(TemporalService::ColorDepthHistory))
		{
			// The color/depth history belongs to the Temporal AA consumer.
			const TemporalConsumerPlan& temporalAA =
				plan.GetConsumer(TemporalConsumer::TemporalAA);
			Invalidate(temporalAA.m_Requested &&
				temporalAA.m_Status == TemporalConsumerStatus::Unavailable
				? TemporalHistoryResetReason::AvailabilityChanged
				: TemporalHistoryResetReason::Disabled);
			return {};
		}

		const TemporalHistoryCompatibilityIdentity compatibility{
			.m_DisplayViewId = plan.m_DisplayViewId,
			.m_ResetIdentity = plan.m_ResetIdentity,
			.m_SessionIdentity = plan.m_SessionIdentity,
			.m_ColorExtent = resolution.m_Display,
			.m_DepthExtent = resolution.m_Render,
			.m_ColorAbi = colorAbi,
			.m_Accumulation = plan.m_HistoryAccumulation,
		};
		const auto isEmpty = [](ViewExtent extent) noexcept
			{
				return extent.m_Width == 0 || extent.m_Height == 0;
			};
		if (compatibility.m_DisplayViewId == RenderViewID::Unknown ||
			compatibility.m_SessionIdentity == 0 || isEmpty(compatibility.m_ColorExtent) ||
			isEmpty(compatibility.m_DepthExtent))
		{
			RecordReset(TemporalHistoryResetReason::AllocationFailure);
			return {};
		}

		const TemporalHistoryResetReason resetReason =
			ResolveCompatibilityResetReason(compatibility);
		if (resetReason != TemporalHistoryResetReason::None && m_ActiveHistory)
		{
			RetireActiveHistory(resetReason, {});
		}
		if (!m_ActiveHistory)
		{
			const bool recordColdStart = !m_HasEstablishedHistory &&
				m_LastResetReason == TemporalHistoryResetReason::None;
			if (!CreateHistorySet(compatibility))
			{
				RecordReset(TemporalHistoryResetReason::AllocationFailure);
				return {};
			}
			m_HasEstablishedHistory = true;
			if (recordColdStart)
			{
				RecordReset(TemporalHistoryResetReason::ColdStart);
			}
		}

		HistorySet& history = *m_ActiveHistory;
		if (history.m_Valid && !IsTemporalColorCompatible(history.m_Compatibility.m_ColorAbi,
			PostProcessColorState::SceneLinearRec709, history.m_LastCommitted.m_PreExposure))
		{
			history.m_Valid = false;
			RecordReset(TemporalHistoryResetReason::InvalidExposureMetadata);
		}
		return {
			.m_AllocationGeneration = history.m_AllocationGeneration,
			.m_ReadIndex = history.m_Textures.GetReadIndex(),
			.m_WriteIndex = history.m_Textures.GetWriteIndex(),
			.m_PreviousPreExposure = history.m_Valid ? history.m_LastCommitted.m_PreExposure : 1.0f,
			.m_Active = true,
			.m_PreviousValid = history.m_Valid && history.m_Textures.IsReadInitialized(),
		};
	}

	bool TemporalHistoryManager::ImportRenderGraphResources(TemporalHistoryFrameState& frame,
		RenderGraph::RGBuilder& builder,
		TemporalHistoryRenderGraphResources& outResources) noexcept
	{
		if (!IsCurrentFrame(frame) || frame.m_Ended || frame.m_RenderGraphImported)
		{
			return false;
		}

		const auto& textures = m_ActiveHistory->m_Textures;
		const bool previousValid = frame.m_PreviousValid && textures.IsReadInitialized();
		// The resolve's writers keep the last committed content of the next textures defined.
		const auto import = [&](HistorySurface surface, const char* previousName,
			const char* nextName) noexcept
			{
				return textures.Import(builder, surface, previousName, nextName, previousValid, true);
			};
		const auto color = import(ColorSurface, "TAA.History.PreviousColor", "TAA.History.NextColor");
		const auto depth = import(DepthSurface, "TAA.History.PreviousDepth", "TAA.History.NextDepth");
		const auto reliability = import(ReliabilitySurface, "TAA.History.PreviousReliability",
			"TAA.History.NextReliability");

		outResources = {
			.m_PreviousColor = color.m_Previous,
			.m_PreviousDepth = depth.m_Previous,
			.m_NextColor = color.m_Next,
			.m_NextDepth = depth.m_Next,
			.m_PreviousReliability = reliability.m_Previous,
			.m_NextReliability = reliability.m_Next,
			.m_ReadIndex = frame.m_ReadIndex,
			.m_WriteIndex = frame.m_WriteIndex,
			.m_PreviousValid = previousValid,
		};
		frame.m_RenderGraphImported = outResources.IsValid();
		return frame.m_RenderGraphImported;
	}

	bool TemporalHistoryManager::ExportRenderGraphResources(TemporalHistoryFrameState& frame,
		RenderGraph::RGBuilder& builder,
		const TemporalHistoryRenderGraphResources& resources) noexcept
	{
		if (!IsCurrentFrame(frame) || frame.m_Ended || !frame.m_RenderGraphImported ||
			frame.m_RenderGraphExported || !resources.IsValid() ||
			resources.m_ReadIndex != frame.m_ReadIndex ||
			resources.m_WriteIndex != frame.m_WriteIndex)
		{
			return false;
		}
		using Textures = TemporalHistoryTextures<HistorySurfaceCount>;
		const std::array<Textures::RenderGraphSurface, HistorySurfaceCount> surfaces{ {
			{ resources.m_PreviousColor, resources.m_NextColor },
			{ resources.m_PreviousDepth, resources.m_NextDepth },
			{ resources.m_PreviousReliability, resources.m_NextReliability },
		} };
		if (!std::ranges::all_of(surfaces, [&builder](const Textures::RenderGraphSurface& surface)
			{
				return Textures::IsFullyWritten(builder, surface);
			}))
		{
			return false;
		}
		for (const Textures::RenderGraphSurface& surface : surfaces)
		{
			Textures::Export(builder, surface, resources.m_PreviousValid);
		}
		frame.m_RenderGraphExported = true;
		return true;
	}

	bool TemporalHistoryManager::CommitFrame(TemporalHistoryFrameState& frame,
		const TemporalHistoryCommittedMetadata& metadata,
		const RHIFencePoint& submittedFence) noexcept
	{
		if (!IsCurrentFrame(frame) || frame.m_Ended || !frame.m_RenderGraphExported ||
			!submittedFence.IsValid() ||
			metadata.m_Compatibility.m_ColorAbi != m_ActiveHistory->m_Compatibility.m_ColorAbi)
		{
			AbortFrame(frame, submittedFence);
			return false;
		}

		if (!IsTemporalColorCompatible(metadata.m_Compatibility.m_ColorAbi,
			PostProcessColorState::SceneLinearRec709, metadata.m_PreExposure))
		{
			AbortFrame(frame, submittedFence);
			RetireActiveHistory(TemporalHistoryResetReason::InvalidExposureMetadata, submittedFence);
			return false;
		}

		HistorySet& history = *m_ActiveHistory;
		// IsCurrentFrame guarantees that the frame wrote the textures this commit promotes.
		history.m_Textures.Commit();
		history.m_Valid = true;
		history.m_LastCommitted = metadata;
		history.m_LastCommitted.m_Compatibility = history.m_Compatibility;
		history.m_LastCommitted.m_GraphicsFence = submittedFence;
		UpdateFence(history.m_LastPossibleUseFence, submittedFence);
		frame.m_Ended = true;
		return true;
	}

	void TemporalHistoryManager::AbortFrame(
		TemporalHistoryFrameState& frame, const RHIFencePoint& retirementFence) noexcept
	{
		if (frame.m_Ended)
		{
			return;
		}
		if (IsCurrentFrame(frame) && frame.m_RenderGraphImported)
		{
			UpdateFence(m_ActiveHistory->m_LastPossibleUseFence, retirementFence);
		}
		frame.m_Ended = true;
	}

	void TemporalHistoryManager::InvalidateAfterFatal(TemporalHistoryFrameState& frame,
		const RHIFencePoint& submittedFence) noexcept
	{
		if (frame.m_Ended)
		{
			return;
		}
		if (IsCurrentFrame(frame))
		{
			RetireActiveHistory(TemporalHistoryResetReason::FatalSubmission, submittedFence);
		}
		frame.m_Ended = true;
	}

	void TemporalHistoryManager::Invalidate(
		TemporalHistoryResetReason reason, const RHIFencePoint& retirementFence) noexcept
	{
		if (m_ActiveHistory)
		{
			RetireActiveHistory(reason, retirementFence);
		}
	}

	void TemporalHistoryManager::Shutdown() noexcept
	{
		if (m_Shutdown)
		{
			return;
		}
		Invalidate(TemporalHistoryResetReason::Shutdown);
		m_TexturePool->Tick();
		m_Shutdown = true;
	}

	TemporalHistorySummary TemporalHistoryManager::GetSummary() const noexcept
	{
		TemporalHistorySummary summary{ .m_LastResetReason = m_LastResetReason };
		if (m_ActiveHistory)
		{
			summary.m_DisplayViewId = m_ActiveHistory->m_Compatibility.m_DisplayViewId;
			summary.m_SessionIdentity = m_ActiveHistory->m_Compatibility.m_SessionIdentity;
			summary.m_HasActiveHistory = true;
			summary.m_HistoryValid = m_ActiveHistory->m_Valid;
		}
		return summary;
	}

	TemporalHistoryManagerDiagnostics TemporalHistoryManager::GetDiagnostics() const
	{
		TemporalHistoryManagerDiagnostics diagnostics{
			.m_LastResetReason = m_LastResetReason,
			.m_ResetCount = m_ResetCount,
		};
		if (m_ActiveHistory)
		{
			const HistorySet& history = *m_ActiveHistory;
			diagnostics.m_Compatibility = history.m_Compatibility;
			diagnostics.m_LastCommitted = history.m_LastCommitted;
			diagnostics.m_AllocationGeneration = history.m_AllocationGeneration;
			diagnostics.m_ReadIndex = history.m_Textures.GetReadIndex();
			diagnostics.m_HasActiveHistory = true;
			diagnostics.m_HistoryValid = history.m_Valid;
			diagnostics.m_ActiveBytes = history.m_Textures.GetEstimatedBytes();
		}

		const PersistentTexturePoolDiagnostics poolDiagnostics = m_TexturePool->GetDiagnostics();
		for (const PersistentTexturePendingRetirementDiagnostics& pending :
			poolDiagnostics.m_PendingRetirements)
		{
			if (std::string_view(pending.m_LogicalName).starts_with(TemporalHistoryNamePrefix))
			{
				diagnostics.m_PendingRetirementBytes += pending.m_EstimatedBytes;
				diagnostics.m_PendingRetirementFences.push_back(pending.m_FencePoint);
			}
		}
		return diagnostics;
	}

	TemporalHistoryResetReason TemporalHistoryManager::ResolveCompatibilityResetReason(
		const TemporalHistoryCompatibilityIdentity& compatibility) const noexcept
	{
		if (!m_ActiveHistory)
		{
			return TemporalHistoryResetReason::None;
		}
		const TemporalHistoryCompatibilityIdentity& current =
			m_ActiveHistory->m_Compatibility;
		if (current.m_DisplayViewId != compatibility.m_DisplayViewId)
		{
			return TemporalHistoryResetReason::DisplayViewChanged;
		}
		if (current.m_ResetIdentity != compatibility.m_ResetIdentity)
		{
			return TemporalHistoryResetReason::ResetIdentityChanged;
		}
		if (current.m_SessionIdentity != compatibility.m_SessionIdentity)
		{
			return TemporalHistoryResetReason::SessionIdentityChanged;
		}
		if (current.m_ColorExtent != compatibility.m_ColorExtent ||
			current.m_DepthExtent != compatibility.m_DepthExtent)
		{
			return TemporalHistoryResetReason::ExtentChanged;
		}
		if (current.m_ColorAbi != compatibility.m_ColorAbi)
		{
			return TemporalHistoryResetReason::ColorAbiChanged;
		}
		if (current.m_Accumulation != compatibility.m_Accumulation)
		{
			return TemporalHistoryResetReason::AccumulationChanged;
		}
		return current.m_ColorFormat != compatibility.m_ColorFormat ||
			current.m_DepthFormat != compatibility.m_DepthFormat ||
			current.m_ReliabilityFormat != compatibility.m_ReliabilityFormat
			? TemporalHistoryResetReason::FormatChanged
			: TemporalHistoryResetReason::None;
	}

	bool TemporalHistoryManager::CreateHistorySet(
		const TemporalHistoryCompatibilityIdentity& compatibility) noexcept
	{
		HistorySet history{};
		history.m_Compatibility = compatibility;
		history.m_AllocationGeneration = m_NextAllocationGeneration++;
		GGLAB_ASSERT_MSG(history.m_AllocationGeneration != 0,
			"Temporal history allocation generation overflowed its valid range.");
		if (history.m_AllocationGeneration == 0)
		{
			return false;
		}

		// Color and reliability follow the display extent, depth the render extent.
		if (!history.m_Textures.Acquire(*m_TexturePool, { {
			{ .m_CreateInfo = MakeHistoryTextureCreateInfo(compatibility.m_ColorFormat,
					compatibility.m_ColorExtent.m_Width, compatibility.m_ColorExtent.m_Height),
				.m_AllocationNames = { "TAA.HistoryColor0", "TAA.HistoryColor1" } },
			{ .m_CreateInfo = MakeHistoryTextureCreateInfo(compatibility.m_DepthFormat,
					compatibility.m_DepthExtent.m_Width, compatibility.m_DepthExtent.m_Height),
				.m_AllocationNames = { "TAA.HistoryDepth0", "TAA.HistoryDepth1" } },
			{ .m_CreateInfo = MakeHistoryTextureCreateInfo(compatibility.m_ReliabilityFormat,
					compatibility.m_ColorExtent.m_Width, compatibility.m_ColorExtent.m_Height),
				.m_AllocationNames = { "TAA.HistoryReliability0", "TAA.HistoryReliability1" } },
			} }))
		{
			return false;
		}

		m_ActiveHistory.emplace(std::move(history));
		return true;
	}

	bool TemporalHistoryManager::IsCurrentFrame(
		const TemporalHistoryFrameState& frame) const noexcept
	{
		return frame.m_Active && m_ActiveHistory &&
			frame.m_AllocationGeneration == m_ActiveHistory->m_AllocationGeneration &&
			frame.m_ReadIndex == m_ActiveHistory->m_Textures.GetReadIndex() &&
			frame.m_WriteIndex == m_ActiveHistory->m_Textures.GetWriteIndex();
	}

	void TemporalHistoryManager::RetireActiveHistory(TemporalHistoryResetReason reason,
		const RHIFencePoint& retirementFence) noexcept
	{
		GGLAB_ASSERT(m_ActiveHistory.has_value());
		HistorySet history = std::move(*m_ActiveHistory);
		m_ActiveHistory.reset();
		UpdateFence(history.m_LastPossibleUseFence, retirementFence);
		history.m_Textures.Release(*m_TexturePool, history.m_LastPossibleUseFence);
		RecordReset(reason);
	}

	void TemporalHistoryManager::RecordReset(TemporalHistoryResetReason reason) noexcept
	{
		if (reason == TemporalHistoryResetReason::None)
		{
			return;
		}
		m_LastResetReason = reason;
		++m_ResetCount;
		GGLAB_ASSERT_MSG(m_ResetCount != 0, "Temporal history reset counter overflowed.");
	}

	void TemporalHistoryManager::UpdateFence(
		RHIFencePoint& destination, const RHIFencePoint& candidate) noexcept
	{
		if (!candidate.IsValid())
		{
			return;
		}
		if (!destination.IsValid())
		{
			destination = candidate;
			return;
		}
		GGLAB_ASSERT_MSG(destination.m_Fence == candidate.m_Fence,
			"Temporal history lifetime fences must belong to one graphics timeline.");
		if (destination.m_Fence == candidate.m_Fence && candidate.m_Value > destination.m_Value)
		{
			destination = candidate;
		}
	}
}
