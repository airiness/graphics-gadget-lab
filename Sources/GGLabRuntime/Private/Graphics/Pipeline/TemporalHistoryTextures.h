#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/Resource/PersistentTexturePool.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace gglab
{
	// Persistent ping-pong textures of one temporal history owner. Every surface is a pair
	// of textures; all surfaces share one read index, so a submitted frame commits them
	// together. The read textures hold the committed history and the write textures receive
	// the next one. Owners keep their own policy: when the history is compatible, how many
	// frames it represents and which fence retires it.
	template <size_t SurfaceCount>
	class TemporalHistoryTextures
	{
	public:
		struct SurfaceDesc
		{
			RHIOwnedTextureCreateInfo m_CreateInfo{};
			std::array<std::string_view, 2> m_AllocationNames{};
		};

		struct RenderGraphSurface
		{
			RGTextureId m_Previous{};
			RGTextureId m_Next{};

			[[nodiscard]] bool IsValid() const noexcept
			{
				return m_Previous.IsValid() && m_Next.IsValid();
			}
		};

		TemporalHistoryTextures() noexcept = default;
		~TemporalHistoryTextures() noexcept
		{
			GGLAB_ASSERT_MSG(!IsAllocated(),
				"Temporal history textures must be released before destruction.");
		}
		TemporalHistoryTextures(TemporalHistoryTextures&&) noexcept = default;
		TemporalHistoryTextures& operator=(TemporalHistoryTextures&&) noexcept = default;
		TemporalHistoryTextures(const TemporalHistoryTextures&) = delete;
		TemporalHistoryTextures& operator=(const TemporalHistoryTextures&) = delete;

		// Acquires every pair or none. A partial allocation never reached a queue, so it is
		// returned without a fence.
		[[nodiscard]] bool Acquire(PersistentTexturePool& pool,
			const std::array<SurfaceDesc, SurfaceCount>& surfaces) noexcept
		{
			GGLAB_ASSERT_MSG(!IsAllocated(), "Temporal history textures are already allocated.");
			for (size_t surface = 0; surface < SurfaceCount; ++surface)
			{
				for (uint32_t index = 0; index < 2; ++index)
				{
					m_Textures[surface][index] = pool.AcquireTexture(
						surfaces[surface].m_CreateInfo, surfaces[surface].m_AllocationNames[index]);
				}
			}
			const bool complete = std::ranges::all_of(m_Textures, [](const auto& pair) noexcept
				{
					return pair[0].IsValid() && pair[1].IsValid();
				});
			if (!complete)
			{
				Release(pool, {});
			}
			return complete;
		}

		// Retires every texture after the gate fence, or at once when no frame used them.
		void Release(PersistentTexturePool& pool, const RHIFencePoint& gate) noexcept
		{
			for (auto& pair : m_Textures)
			{
				for (PersistentTextureAllocation& allocation : pair)
				{
					if (!allocation.IsValid())
					{
						continue;
					}
					const bool released = gate.IsValid()
						? pool.ReleaseTexture(std::move(allocation), gate)
						: pool.ReleaseTextureWithoutSubmission(std::move(allocation));
					GGLAB_ASSERT_MSG(released, "Temporal history textures must retire through their pool.");
					GGLAB_UNUSED(released);
				}
			}
			m_Initialized = {};
			m_ReadIndex = 0;
		}

		[[nodiscard]] bool IsAllocated() const noexcept
		{
			return std::ranges::any_of(m_Textures, [](const auto& pair) noexcept
				{
					return pair[0].IsValid() || pair[1].IsValid();
				});
		}
		[[nodiscard]] uint32_t GetReadIndex() const noexcept { return m_ReadIndex; }
		[[nodiscard]] uint32_t GetWriteIndex() const noexcept { return 1u - m_ReadIndex; }
		// The read textures hold a committed history.
		[[nodiscard]] bool IsReadInitialized() const noexcept { return m_Initialized[m_ReadIndex]; }

		// Imports one surface. The previous texture is defined only when the owner continues
		// its history. The next texture is written by the frame; an owner whose writers rely
		// on its last committed content keeps that content defined once initialized.
		[[nodiscard]] RenderGraphSurface Import(RenderGraph::RGBuilder& builder, size_t surface,
			const char* previousName, const char* nextName, bool previousValid,
			bool nextDefinedWhenInitialized) const noexcept
		{
			const uint32_t readIndex = m_ReadIndex;
			const uint32_t writeIndex = 1u - m_ReadIndex;
			const PersistentTextureAllocation& previous = m_Textures[surface][readIndex];
			const PersistentTextureAllocation& next = m_Textures[surface][writeIndex];
			return {
				.m_Previous = builder.ImportTexture(previousName, previous.GetTexture(),
					previous.GetCreateInfo().m_Desc,
					m_Initialized[readIndex] ? CommonRHIResourceState() : UndefinedRHITextureState(),
					previousValid ? RGContentValidity::Defined : RGContentValidity::Undefined),
				.m_Next = builder.ImportTexture(nextName, next.GetTexture(),
					next.GetCreateInfo().m_Desc,
					m_Initialized[writeIndex] ? CommonRHIResourceState() : UndefinedRHITextureState(),
					nextDefinedWhenInitialized && m_Initialized[writeIndex]
						? RGContentValidity::Defined
						: RGContentValidity::Undefined),
			};
		}

		[[nodiscard]] static bool IsFullyWritten(
			RenderGraph::RGBuilder& builder, const RenderGraphSurface& surface) noexcept
		{
			return builder.IsTextureFullyWrittenByCurrentPass(surface.m_Next);
		}

		// Returns both textures of a surface to the common state its next import expects.
		static void Export(RenderGraph::RGBuilder& builder, const RenderGraphSurface& surface,
			bool previousValid) noexcept
		{
			if (previousValid)
			{
				builder.Export(surface.m_Previous, RGTextureAccess::None);
			}
			builder.Export(surface.m_Next, RGTextureAccess::None);
		}

		// The frame that wrote the next textures was submitted: they become the history.
		void Commit() noexcept
		{
			m_ReadIndex = 1u - m_ReadIndex;
			m_Initialized[m_ReadIndex] = true;
		}

		[[nodiscard]] uint64_t GetEstimatedBytes() const noexcept
		{
			uint64_t bytes = 0;
			for (const auto& pair : m_Textures)
			{
				bytes += pair[0].GetEstimatedBytes() + pair[1].GetEstimatedBytes();
			}
			return bytes;
		}

	private:
		std::array<std::array<PersistentTextureAllocation, 2>, SurfaceCount> m_Textures;
		std::array<bool, 2> m_Initialized{};
		uint32_t m_ReadIndex = 0;
	};
}
