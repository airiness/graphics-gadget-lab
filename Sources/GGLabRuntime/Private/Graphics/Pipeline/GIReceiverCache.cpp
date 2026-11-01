#include "Graphics/Pipeline/GIReceiverCache.h"
#include "Graphics/GIShaderConstants.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	static_assert(GIObservationLimits::MaximumReceiverQueriesPerPixel == gi_shader::GIReceiverMaximumQueries);

	GIReceiverCache::GIReceiverCache(RHIDevice& device, PersistentTexturePool& pool,
		std::array<std::string_view, SurfaceCount> names) noexcept :
		m_Device(device), m_Pool(pool), m_Names(names) {}

	GIReceiverCache::~GIReceiverCache()
	{
		GGLAB_ASSERT_MSG(!m_Textures[0].IsValid(), "Receiver cache requires checked observation teardown.");
	}

	void GIReceiverCache::BeginFrame(uint64_t serial) noexcept
	{
		m_FrameSerial = serial;
		m_BatchPixels = 0;
		m_Exported = false;
		m_Recorded = false;
		m_Settled = false;
		if (m_PendingFence.IsValid())
		{
			const auto query = m_Device.QueryFencePoint(m_PendingFence);
			if (query == RHIFenceQueryResult::Completed)
			{
				m_CompletedPixels = m_SubmittedPixels;
				m_PendingFence.Reset();
			}
			else if (query != RHIFenceQueryResult::Pending)
			{
				m_Failed = true;
			}
		}
	}

	bool GIReceiverCache::Prepare(uint32_t width, uint32_t height, uint32_t queryBudget) noexcept
	{
		const uint64_t pixels = uint64_t(width) * height;
		if (m_UnknownCompletion || m_Failed || pixels == 0 ||
			pixels > GIObservationLimits::MaximumImagePixels ||
			queryBudget < gi_shader::GIReceiverMaximumQueries ||
			queryBudget > GIObservationLimits::MaximumReceiverQueryWorkPerSubmission)
		{
			return false;
		}
		if (!m_Textures[0].IsValid())
		{
			m_Width = width;
			m_Height = height;
			for (size_t index = 0; index < SurfaceCount; ++index)
			{
				m_Textures[index] = m_Pool.AcquireTexture({ .m_Desc = {
					.m_Format = index < 2 ? RHIFormat::R32G32B32A32Float : RHIFormat::R32Uint,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess | RHITextureUsage::CopySource,
					.m_Extent = { width, height, 1 } } }, m_Names[index]);
				if (!m_Textures[index].IsValid())
				{
					Release();
					return false;
				}
			}
		}
		if (m_Width != width || m_Height != height)
		{
			return false;
		}
		m_QueryBudget = queryBudget;
		if (!m_PendingFence.IsValid())
		{
			m_BatchPixels = std::min(queryBudget / gi_shader::GIReceiverMaximumQueries,
				static_cast<uint32_t>(pixels) - m_SubmittedPixels);
		}
		return true;
	}

	GIReceiverCache::Views GIReceiverCache::Import(RenderGraph::RGBuilder& builder) noexcept
	{
		Views views{};
		const bool writing = m_BatchPixels != 0;
		if (!writing && !GetProgress().m_Ready)
		{
			return views;
		}
		for (size_t index = 0; index < SurfaceCount; ++index)
		{
			const auto& texture = m_Textures[index];
			auto resource = builder.ImportTexture(m_Names[index].data(), texture.GetTexture(),
				texture.GetCreateInfo().m_Desc,
				m_Initialized ? CommonRHIResourceState() : UndefinedRHITextureState(),
				m_Initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
			if (writing)
			{
				if (m_Initialized)
				{
					builder.ReadWriteInPlace(resource, RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
				}
				else
				{
					// The first preparation defines all pixels, not only its query range.
					builder.WriteInPlace(resource, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				}
				views[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(resource);
				GGLAB_ASSERT(builder.IsTextureFullyWrittenByCurrentPass(resource));
			}
			else
			{
				views[index] = builder.CreateView<RHITextureViewType::ShaderResource>(
					builder.Read(resource, RGTextureAccess::Sample, RHIStage::ComputeShader));
			}
			builder.Export(resource, RGTextureAccess::None);
		}
		m_Exported = true;
		return views;
	}

	std::array<uint32_t, GIReceiverCache::SurfaceCount> GIReceiverCache::ResolveDescriptors(
		const RGExecuteContext& execute, const Views& views) noexcept
	{
		// Unimported caches (including stateless reference work) keep their zero descriptors.
		std::array<uint32_t, SurfaceCount> descriptors{};
		for (size_t index = 0; index < views.size(); ++index)
		{
			if (views[index].IsValid())
			{
				descriptors[index] = execute.GetViewDescriptor(views[index]).m_Index;
			}
		}
		return descriptors;
	}

	bool GIReceiverCache::CanSample() const noexcept
	{
		// A small image may prepare and sample in the same invocation. A multi-frame
		// preparation must finish before any whole-image sample sweep can start.
		return (GetProgress().m_Ready && !m_PendingFence.IsValid()) ||
			(m_BatchPixels != 0 && m_BatchPixels == m_Width * m_Height);
	}

	void GIReceiverCache::Settle(const RenderFrameOutcome& outcome) noexcept
	{
		GGLAB_ASSERT(outcome.m_FrameSerial == m_FrameSerial);
		if (m_Settled)
		{
			return;
		}
		m_Settled = true;
		if (outcome.m_Kind == RenderFrameOutcomeKind::CompletionUnknown)
		{
			m_UnknownCompletion = true;
			m_Failed = true;
			return;
		}
		if (m_Exported && outcome.m_Fence.IsValid())
		{
			m_LastUse = outcome.m_Fence;
		}
		if (outcome.m_Kind == RenderFrameOutcomeKind::Failed)
		{
			m_Failed = true;
		}
		else if (outcome.m_Kind == RenderFrameOutcomeKind::Submitted &&
			m_Exported && m_Recorded && m_BatchPixels != 0)
		{
			GGLAB_ASSERT(outcome.m_Fence.IsValid());
			m_Initialized = true;
			m_SubmittedPixels += m_BatchPixels;
			m_PendingFence = outcome.m_Fence;
		}
	}

	GIReceiverPreparationProgress GIReceiverCache::GetProgress() const noexcept
	{
		uint64_t bytes = 0;
		for (const auto& texture : m_Textures)
		{
			bytes += texture.GetEstimatedBytes();
		}
		const uint32_t pixels = m_Width * m_Height;
		return { .m_TotalPixels = pixels, .m_SubmittedPixels = m_SubmittedPixels,
			.m_CompletedPixels = m_CompletedPixels, .m_BatchPixels = m_BatchPixels,
			.m_QueryWorkPerSubmission = m_QueryBudget,
			.m_BatchQueryWork = uint64_t(m_BatchPixels) * gi_shader::GIReceiverMaximumQueries,
			.m_CacheBytes = bytes, .m_Ready = pixels != 0 && m_SubmittedPixels == pixels && !m_Failed };
	}

	void GIReceiverCache::Release() noexcept
	{
		if (m_UnknownCompletion)
		{
			return;
		}
		for (auto& texture : m_Textures)
		{
			if (!texture.IsValid())
			{
				continue;
			}
			const bool released = m_LastUse.IsValid() ?
				m_Pool.ReleaseTexture(std::move(texture), m_LastUse) :
				m_Pool.ReleaseTextureWithoutSubmission(std::move(texture));
			GGLAB_ASSERT(released);
			GGLAB_UNUSED(released);
		}
		m_LastUse.Reset();
		m_PendingFence.Reset();
		m_Width = 0;
		m_Height = 0;
		m_SubmittedPixels = 0;
		m_CompletedPixels = 0;
		m_BatchPixels = 0;
		m_QueryBudget = 0;
		m_Initialized = false;
		m_Exported = false;
		m_Recorded = false;
		m_Failed = false;
	}

	void GIReceiverCache::Shutdown() noexcept
	{
		m_UnknownCompletion = false;
		Release();
	}
}
