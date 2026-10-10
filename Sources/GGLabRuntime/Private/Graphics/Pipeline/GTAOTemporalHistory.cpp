#include "Graphics/Pipeline/GTAOTemporalHistory.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

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
	}

	GTAOTemporalHistory::GTAOTemporalHistory(PersistentTexturePool* texturePool) noexcept :
		m_TexturePool(texturePool)
	{
	}

	GTAOTemporalHistory::~GTAOTemporalHistory() noexcept
	{
		GGLAB_ASSERT_MSG(!m_Textures.IsAllocated(),
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

		if (m_Extent != halfExtent || !m_Textures.IsAllocated())
		{
			Release(retirementFence);
			if (!m_Textures.Acquire(*m_TexturePool, { {
				{ .m_CreateInfo =
						MakeHistoryTextureCreateInfo(GTAOHistoryVisibilityFormat, halfExtent),
					.m_AllocationNames = { "GTAO.HistoryVisibility0", "GTAO.HistoryVisibility1" } },
				{ .m_CreateInfo = MakeHistoryTextureCreateInfo(GTAOHistoryViewZFormat, halfExtent),
					.m_AllocationNames = { "GTAO.HistoryViewZ0", "GTAO.HistoryViewZ1" } },
				} }))
			{
				GGLAB_LOG_GRAPHICS_ERROR("Temporal GTAO failed to allocate its {}x{} history.",
					halfExtent.m_Width, halfExtent.m_Height);
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
		const bool previousValid =
			continuesPreviousView && m_CommittedValid && m_Textures.IsReadInitialized();
		// The temporal pass overwrites every texel of the next textures.
		const auto visibility = m_Textures.Import(builder, VisibilitySurface,
			"GTAO.PreviousVisibility", "GTAO.NextVisibility", previousValid, false);
		const auto viewZ = m_Textures.Import(builder, ViewZSurface, "GTAO.PreviousViewZ",
			"GTAO.NextViewZ", previousValid, false);
		outResources = {
			.m_PreviousVisibility = visibility.m_Previous,
			.m_PreviousViewZ = viewZ.m_Previous,
			.m_NextVisibility = visibility.m_Next,
			.m_NextViewZ = viewZ.m_Next,
			.m_PreviousValid = previousValid,
		};
		m_Imported = true;
		return true;
	}

	bool GTAOTemporalHistory::ExportRenderGraphResources(RenderGraph::RGBuilder& builder,
		const GTAOTemporalHistoryRenderGraphResources& resources) noexcept
	{
		using Textures = TemporalHistoryTextures<HistorySurfaceCount>;
		const Textures::RenderGraphSurface visibility{
			resources.m_PreviousVisibility, resources.m_NextVisibility };
		const Textures::RenderGraphSurface viewZ{ resources.m_PreviousViewZ, resources.m_NextViewZ };
		if (!m_Imported || m_Exported || !resources.IsValid() ||
			!Textures::IsFullyWritten(builder, visibility) ||
			!Textures::IsFullyWritten(builder, viewZ))
		{
			return false;
		}
		Textures::Export(builder, visibility, resources.m_PreviousValid);
		Textures::Export(builder, viewZ, resources.m_PreviousValid);
		m_Exported = true;
		return true;
	}

	void GTAOTemporalHistory::CommitFrame() noexcept
	{
		if (m_Active)
		{
			if (m_Exported)
			{
				m_Textures.Commit();
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
		// The renderer's last submitted fence covers every frame that used the pairs.
		if (m_TexturePool)
		{
			m_Textures.Release(*m_TexturePool, retirementFence);
		}
		m_Extent = {};
		m_CommittedValid = false;
		AbortFrame();
	}
}
