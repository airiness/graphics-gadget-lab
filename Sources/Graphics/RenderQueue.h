#pragma once
#include "Core/Math/Culling.h"
#include "Core/Math/Vector.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Pipeline/DepthCoverage.h"
#include "Graphics/RenderScene.h"

#include <span>

namespace gglab
{
	struct DrawItem
	{
		RenderMaterialKey m_MaterialKey{};
		DepthCoverageDrawPacket m_CoverageDrawPacket{};

		RenderBucket m_Bucket = RenderBucket::Opaque;
		uint64_t m_VariantBits = 0;
		uint64_t m_SortKey = 0;
		uint32_t mDepthKey = 0;
	};

	struct DrawItemsRange
	{
		uint32_t m_Start = 0;
		uint32_t m_Count = 0;
	};

	struct RenderQueueStatistics
	{
		uint32_t m_TotalInstanceCount = 0;
		uint32_t m_VisibleInstanceCount = 0;
		uint32_t m_CulledInstanceCount = 0;
		uint32_t m_InvalidInstanceCount = 0;
		uint32_t m_UnboundedInstanceCount = 0;
		uint32_t m_DrawItemCount = 0;
	};

	struct RenderQueue
	{
		std::vector<DrawItem> m_DrawItems;

		std::array<DrawItemsRange, utils::ToIndex(RenderBucket::Count)> m_BucketDrawRanges{};

		RenderViewID m_ViewId = RenderViewID::Unknown;
		DepthCoverageRasterDomain m_CoverageRasterDomain{};
		RenderQueueStatistics m_Statistics{};
	};

	class RenderQueueBuilder
	{
	public:
		struct BuildInfo
		{
			AssetManager& m_AssetManager;
			const RenderScene& m_RenderScene;
			const RenderView& m_RenderView;
			std::span<const math::Frustum> m_CullingFrustums;
			DepthCoverageRasterDomain m_CoverageRasterDomain{};
			RHIBufferHandle m_ObjectBuffer{};
			uint32_t m_ObjectBaseIndex = 0;
			RHIBufferHandle m_MaterialBuffer{};
			uint32_t m_MaterialBaseIndex = 0;
		};

		enum VariantBit : uint64_t
		{
			DoubleSided = 1ull << 0,

			BucketShift = 1,
			BucketMask = 0x3ull << BucketShift,
		};

		static constexpr uint32_t VariantBitCount = 8;
		static constexpr uint32_t VariantCount = 1u << VariantBitCount;
		static constexpr uint64_t VariantMask = (1ull << VariantBitCount) - 1ull;
		static_assert((BucketMask & ~VariantMask) == 0);

	public:
		RenderQueue Build(const BuildInfo& info) noexcept;

		static constexpr bool DecodeVariantDoubleSided(uint64_t variantBits) noexcept
		{
			return (variantBits & VariantBit::DoubleSided) != 0;
		}
		static constexpr RenderBucket DecodeVariantBucket(uint64_t variantBits) noexcept
		{
			return static_cast<RenderBucket>((variantBits & VariantBit::BucketMask) >> VariantBit::BucketShift);
		}
		static constexpr uint64_t EncodeVariantBits(
			RenderBucket bucket,
			bool doubleSided) noexcept
		{
			const uint64_t bucketBits =
				(static_cast<uint64_t>(bucket) <<
					VariantBit::BucketShift) &
				VariantBit::BucketMask;
			const uint64_t sidednessBits =
				doubleSided ?
					VariantBit::DoubleSided :
					0ull;
			return bucketBits | sidednessBits;
		}
	private:
		static constexpr uint8_t BucketSortOrder(RenderBucket bucket) noexcept;
		static constexpr uint64_t PackSortKey(uint8_t bucketOrder,
			uint8_t variantBits,
			uint32_t materialKey,
			uint32_t meshKey) noexcept;
		static RenderBucket DecideRenderBucket(AlphaMode alphaMode) noexcept;
		static bool IsDoubleSided(MaterialFlags flags) noexcept;
		static uint32_t MakeDepthKey(const RenderView& renderView, const Vector3& worldCenterPos) noexcept;
	};
}
