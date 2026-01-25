#pragma once
#include "GraphicsTypes.h"
#include "RenderScene.h"

namespace gglab
{
	struct DrawItem
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
		uint32_t m_ObjectOffset = 0;

		RenderBucket m_Bucket = RenderBucket::Opaque;
		uint64_t m_VariantBits = 0;
		uint64_t m_SortKey = 0;
	};

	struct DrawItemsRange
	{
		uint32_t m_Start = 0;
		uint32_t m_Count = 0;
	};

	struct RenderQueue
	{
		std::vector<DrawItem> m_DrawItems;

		std::array<DrawItemsRange, utils::ToIndex(RenderBucket::Count)> m_BucketDrawRanges{};
	};

	class RenderQueueBuilder
	{
	public:
		struct BuildInfo
		{
			AssetManager& m_AssetManager;
			const RenderScene& m_RenderScene;
		};

		enum VariantBit : uint64_t
		{
			DoubleSided = 1ull << 0,

			BucketShift = 1,
			BucketMask = 0x3ull << BucketShift,
		};

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
	private:
		static constexpr uint8_t BucketSortOrder(RenderBucket bucket) noexcept;
		static constexpr uint64_t EncodeVariantBits(RenderBucket bucket, bool doubleSided) noexcept;
		static constexpr uint64_t PackSortKey(uint8_t bucketOrder,
			uint16_t variantBits,
			uint32_t materialKey,
			uint32_t meshKey) noexcept;
		static RenderBucket DecideRenderBucket(const Material* material) noexcept;
		static bool IsDoubleSided(const Material* material) noexcept;
	};
}