#include "Graphics/Pipeline/GTAOTemporalHistory.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] RHIOwnedTextureCreateInfo MakeHistoryTextureCreateInfo(
			RHIFormat format, const GTAOExtent& extent) noexcept
		{
			return {
				.m_Desc = {
					.m_Format = format,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
					.m_Extent = { extent.m_Width, extent.m_Height, 1 },
				},
				.m_InitialState = UndefinedRHITextureState(),
			};
		}

		[[nodiscard]] RGTextureId ImportHistoryTexture(RenderGraph::RGBuilder& builder,
			const char* name, const PersistentTextureAllocation& allocation, bool initialized,
			bool defined) noexcept
		{
			return builder.ImportTexture(name, allocation.GetTexture(),
				allocation.GetCreateInfo().m_Desc,
				initialized ? CommonRHIResourceState() : UndefinedRHITextureState(),
				defined ? RGContentValidity::Defined : RGContentValidity::Undefined);
		}
	}

	GTAOTemporalHistory::GTAOTemporalHistory(PersistentTexturePool* texturePool) noexcept :
		m_TexturePool(texturePool)
	{
	}

	GTAOTemporalHistory::~GTAOTemporalHistory() noexcept
	{
		GGLAB_ASSERT_MSG(!m_Visibility[0].IsValid() && !m_Visibility[1].IsValid() &&
			!m_ViewZ[0].IsValid() && !m_ViewZ[1].IsValid(),
			"Temporal GTAO history must be released before destruction.");
	}

	bool GTAOTemporalHistory::BeginFrame(bool active, const GTAOExtent& halfExtent,
		const RHIFencePoint& retirementFence) noexcept
	{
		AbortFrame();
		if (!active)
		{
			Release(retirementFence);
			return true;
		}
		GGLAB_ASSERT_MSG(halfExtent.IsValid(), "Temporal GTAO requires a non-empty extent.");
		if (!halfExtent.IsValid() || !m_TexturePool)
		{
			return false;
		}

		if (m_Extent != halfExtent || !m_Visibility[0].IsValid())
		{
			Release(retirementFence);
			const RHIOwnedTextureCreateInfo visibilityInfo =
				MakeHistoryTextureCreateInfo(GTAOHistoryVisibilityFormat, halfExtent);
			const RHIOwnedTextureCreateInfo viewZInfo =
				MakeHistoryTextureCreateInfo(GTAOHistoryViewZFormat, halfExtent);
			m_Visibility[0] =
				m_TexturePool->AcquireTexture(visibilityInfo, "GTAO.HistoryVisibility0");
			m_Visibility[1] =
				m_TexturePool->AcquireTexture(visibilityInfo, "GTAO.HistoryVisibility1");
			m_ViewZ[0] = m_TexturePool->AcquireTexture(viewZInfo, "GTAO.HistoryViewZ0");
			m_ViewZ[1] = m_TexturePool->AcquireTexture(viewZInfo, "GTAO.HistoryViewZ1");
			if (!m_Visibility[0].IsValid() || !m_Visibility[1].IsValid() ||
				!m_ViewZ[0].IsValid() || !m_ViewZ[1].IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR("Temporal GTAO failed to allocate its {}x{} history.",
					halfExtent.m_Width, halfExtent.m_Height);
				Release(retirementFence);
				return false;
			}
			m_Extent = halfExtent;
		}
		m_Active = true;
		return true;
	}

	bool GTAOTemporalHistory::ImportRenderGraphResources(RenderGraph::RGBuilder& builder,
		bool continuesPreviousView, GTAOTemporalHistoryRenderGraphResources& outResources) noexcept
	{
		if (!m_Active || m_Imported)
		{
			return false;
		}
		const uint32_t readIndex = m_ReadIndex;
		const uint32_t writeIndex = 1u - m_ReadIndex;
		const bool previousValid =
			continuesPreviousView && m_CommittedValid && m_Initialized[readIndex];
		outResources = {
			.m_PreviousVisibility = ImportHistoryTexture(builder, "GTAO.PreviousVisibility",
				m_Visibility[readIndex], m_Initialized[readIndex], previousValid),
			.m_PreviousViewZ = ImportHistoryTexture(builder, "GTAO.PreviousViewZ",
				m_ViewZ[readIndex], m_Initialized[readIndex], previousValid),
			.m_NextVisibility = ImportHistoryTexture(builder, "GTAO.NextVisibility",
				m_Visibility[writeIndex], m_Initialized[writeIndex], false),
			.m_NextViewZ = ImportHistoryTexture(builder, "GTAO.NextViewZ",
				m_ViewZ[writeIndex], m_Initialized[writeIndex], false),
			.m_PreviousValid = previousValid,
		};
		m_Imported = true;
		return true;
	}

	bool GTAOTemporalHistory::ExportRenderGraphResources(RenderGraph::RGBuilder& builder,
		const GTAOTemporalHistoryRenderGraphResources& resources) noexcept
	{
		if (!m_Imported || m_Exported || !resources.IsValid() ||
			!builder.IsTextureFullyWrittenByCurrentPass(resources.m_NextVisibility) ||
			!builder.IsTextureFullyWrittenByCurrentPass(resources.m_NextViewZ))
		{
			return false;
		}
		if (resources.m_PreviousValid)
		{
			builder.Export(resources.m_PreviousVisibility, RGTextureAccess::None);
			builder.Export(resources.m_PreviousViewZ, RGTextureAccess::None);
		}
		builder.Export(resources.m_NextVisibility, RGTextureAccess::None);
		builder.Export(resources.m_NextViewZ, RGTextureAccess::None);
		m_Exported = true;
		return true;
	}

	void GTAOTemporalHistory::CommitFrame() noexcept
	{
		if (m_Active)
		{
			if (m_Exported)
			{
				m_ReadIndex = 1u - m_ReadIndex;
				m_Initialized[m_ReadIndex] = true;
			}
			m_CommittedValid = m_Exported;
		}
		AbortFrame();
	}

	void GTAOTemporalHistory::AbortFrame() noexcept
	{
		m_Active = false;
		m_Imported = false;
		m_Exported = false;
	}

	void GTAOTemporalHistory::Release(const RHIFencePoint& retirementFence) noexcept
	{
		const auto release = [this, &retirementFence](PersistentTextureAllocation& allocation)
			{
				if (!allocation.IsValid())
				{
					return;
				}
				// The renderer's last submitted fence covers every frame that used the pair.
				const bool released = retirementFence.IsValid()
					? m_TexturePool->ReleaseTexture(std::move(allocation), retirementFence)
					: m_TexturePool->ReleaseTextureWithoutSubmission(std::move(allocation));
				GGLAB_ASSERT_MSG(released, "Temporal GTAO history must retire through its pool.");
			};
		for (PersistentTextureAllocation& allocation : m_Visibility)
		{
			release(allocation);
		}
		for (PersistentTextureAllocation& allocation : m_ViewZ)
		{
			release(allocation);
		}
		m_Initialized = {};
		m_Extent = {};
		m_ReadIndex = 0;
		m_CommittedValid = false;
		AbortFrame();
	}
}
