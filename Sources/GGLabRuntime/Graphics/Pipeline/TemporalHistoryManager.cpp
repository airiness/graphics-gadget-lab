#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"

#include <algorithm>
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
		const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(!m_Shutdown, "Temporal history cannot begin after shutdown.");
		if (m_Shutdown)
		{
			return {};
		}

		if (!plan.m_Active)
		{
			Invalidate(TemporalHistoryResetReason::Disabled);
			return {};
		}

		const TemporalHistoryCompatibilityIdentity compatibility{
			.m_DisplayViewId = plan.m_DisplayViewId,
			.m_ResetIdentity = plan.m_ResetIdentity,
			.m_SessionIdentity = plan.m_SessionIdentity,
			.m_Width = width,
			.m_Height = height,
		};
		if (compatibility.m_DisplayViewId == RenderViewID::Unknown ||
			compatibility.m_SessionIdentity == 0 || width == 0 || height == 0)
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

		const HistorySet& history = *m_ActiveHistory;
		return {
			.m_AllocationGeneration = history.m_AllocationGeneration,
			.m_ReadIndex = history.m_ReadIndex,
			.m_WriteIndex = 1u - history.m_ReadIndex,
			.m_Active = true,
			.m_PreviousValid = history.m_Valid && history.m_Initialized[history.m_ReadIndex],
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

		HistorySet& history = *m_ActiveHistory;
		const uint32_t readIndex = frame.m_ReadIndex;
		const uint32_t writeIndex = frame.m_WriteIndex;
		const bool previousValid =
			frame.m_PreviousValid && history.m_Initialized[readIndex];
		const RHIResourceState previousInitialState = history.m_Initialized[readIndex]
			? CommonRHIResourceState()
			: UndefinedRHITextureState();
		const RHIResourceState nextInitialState = history.m_Initialized[writeIndex]
			? CommonRHIResourceState()
			: UndefinedRHITextureState();

		outResources = {
			.m_PreviousColor = builder.ImportTexture("TAA.History.PreviousColor",
				history.m_Color[readIndex].GetTexture(),
				history.m_Color[readIndex].GetCreateInfo().m_Desc, previousInitialState,
				previousValid ? RGContentValidity::Defined : RGContentValidity::Undefined),
			.m_PreviousDepth = builder.ImportTexture("TAA.History.PreviousDepth",
				history.m_Depth[readIndex].GetTexture(),
				history.m_Depth[readIndex].GetCreateInfo().m_Desc, previousInitialState,
				previousValid ? RGContentValidity::Defined : RGContentValidity::Undefined),
			.m_NextColor = builder.ImportTexture("TAA.History.NextColor",
				history.m_Color[writeIndex].GetTexture(),
				history.m_Color[writeIndex].GetCreateInfo().m_Desc, nextInitialState,
				history.m_Initialized[writeIndex] ? RGContentValidity::Defined
												  : RGContentValidity::Undefined),
			.m_NextDepth = builder.ImportTexture("TAA.History.NextDepth",
				history.m_Depth[writeIndex].GetTexture(),
				history.m_Depth[writeIndex].GetCreateInfo().m_Desc, nextInitialState,
				history.m_Initialized[writeIndex] ? RGContentValidity::Defined
												  : RGContentValidity::Undefined),
			.m_ReadIndex = readIndex,
			.m_WriteIndex = writeIndex,
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
		if (!builder.IsTextureFullyWrittenByCurrentPass(resources.m_NextColor) ||
			!builder.IsTextureFullyWrittenByCurrentPass(resources.m_NextDepth))
		{
			return false;
		}

		if (resources.m_PreviousValid)
		{
			builder.Export(resources.m_PreviousColor, RGTextureAccess::None);
			builder.Export(resources.m_PreviousDepth, RGTextureAccess::None);
		}
		builder.Export(resources.m_NextColor, RGTextureAccess::None);
		builder.Export(resources.m_NextDepth, RGTextureAccess::None);
		frame.m_RenderGraphExported = true;
		return true;
	}

	bool TemporalHistoryManager::CommitFrame(TemporalHistoryFrameState& frame,
		const TemporalHistoryCommittedMetadata& metadata,
		const RHIFencePoint& submittedFence) noexcept
	{
		if (!IsCurrentFrame(frame) || frame.m_Ended || !frame.m_RenderGraphExported ||
			!submittedFence.IsValid())
		{
			AbortFrame(frame, submittedFence);
			return false;
		}

		HistorySet& history = *m_ActiveHistory;
		history.m_ReadIndex = frame.m_WriteIndex;
		history.m_Initialized[frame.m_WriteIndex] = true;
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
			diagnostics.m_ReadIndex = history.m_ReadIndex;
			diagnostics.m_HasActiveHistory = true;
			diagnostics.m_HistoryValid = history.m_Valid;
			for (uint32_t index = 0; index < 2; ++index)
			{
				diagnostics.m_ActiveBytes += history.m_Color[index].GetEstimatedBytes();
				diagnostics.m_ActiveBytes += history.m_Depth[index].GetEstimatedBytes();
			}
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
		if (current.m_Width != compatibility.m_Width || current.m_Height != compatibility.m_Height)
		{
			return TemporalHistoryResetReason::ExtentChanged;
		}
		return current.m_ColorFormat != compatibility.m_ColorFormat ||
			current.m_DepthFormat != compatibility.m_DepthFormat
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

		const RHIOwnedTextureCreateInfo colorInfo = MakeHistoryTextureCreateInfo(
			compatibility.m_ColorFormat, compatibility.m_Width, compatibility.m_Height);
		const RHIOwnedTextureCreateInfo depthInfo = MakeHistoryTextureCreateInfo(
			compatibility.m_DepthFormat, compatibility.m_Width, compatibility.m_Height);
		history.m_Color[0] = m_TexturePool->AcquireTexture(colorInfo, "TAA.HistoryColor0");
		history.m_Color[1] = m_TexturePool->AcquireTexture(colorInfo, "TAA.HistoryColor1");
		history.m_Depth[0] = m_TexturePool->AcquireTexture(depthInfo, "TAA.HistoryDepth0");
		history.m_Depth[1] = m_TexturePool->AcquireTexture(depthInfo, "TAA.HistoryDepth1");
		const bool complete = std::ranges::all_of(history.m_Color,
			&PersistentTextureAllocation::IsValid) &&
			std::ranges::all_of(history.m_Depth, &PersistentTextureAllocation::IsValid);
		if (!complete)
		{
			for (PersistentTextureAllocation& allocation : history.m_Color)
			{
				if (allocation.IsValid())
				{
					GGLAB_UNUSED(
						m_TexturePool->ReleaseTextureWithoutSubmission(std::move(allocation)));
				}
			}
			for (PersistentTextureAllocation& allocation : history.m_Depth)
			{
				if (allocation.IsValid())
				{
					GGLAB_UNUSED(
						m_TexturePool->ReleaseTextureWithoutSubmission(std::move(allocation)));
				}
			}
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
			frame.m_ReadIndex == m_ActiveHistory->m_ReadIndex &&
			frame.m_WriteIndex == 1u - m_ActiveHistory->m_ReadIndex;
	}

	void TemporalHistoryManager::RetireActiveHistory(TemporalHistoryResetReason reason,
		const RHIFencePoint& retirementFence) noexcept
	{
		GGLAB_ASSERT(m_ActiveHistory.has_value());
		HistorySet history = std::move(*m_ActiveHistory);
		m_ActiveHistory.reset();
		UpdateFence(history.m_LastPossibleUseFence, retirementFence);
		const RHIFencePoint gate = history.m_LastPossibleUseFence;
		auto release = [&](PersistentTextureAllocation& allocation)
		{
			const bool released = gate.IsValid()
				? m_TexturePool->ReleaseTexture(std::move(allocation), gate)
				: m_TexturePool->ReleaseTextureWithoutSubmission(std::move(allocation));
			GGLAB_ASSERT_MSG(released, "Temporal history texture retirement failed.");
		};
		for (PersistentTextureAllocation& allocation : history.m_Color)
		{
			release(allocation);
		}
		for (PersistentTextureAllocation& allocation : history.m_Depth)
		{
			release(allocation);
		}
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
