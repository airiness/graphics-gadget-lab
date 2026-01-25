#include "Precompiled.h"
#include "RenderQueue.h"
#include "AssetManager.h"

namespace gglab
{
	RenderQueue RenderQueueBuilder::Build(const BuildInfo& info) noexcept
	{
		RenderQueue renderQueue{};

		const auto& instances = info.m_RenderScene.m_RenderInstances;
		renderQueue.m_DrawItems.reserve(instances.size());

		for (const auto& instance : instances)
		{
			const auto* mesh = info.m_AssetManager.GetMesh(instance.m_MeshId);
			if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
			{
				GGLAB_LOG_GRAPHICS_INFO("RenderQueueBuilder: Mesh is invalid, skip it.");
				continue;
			}

			const Material* material = info.m_AssetManager.GetMaterial(instance.m_MaterialId);

			RenderBucket bucket = DecideRenderBucket(material);
			bool doubleSided = IsDoubleSided(material);

			uint64_t variantBits = 0;
			variantBits |= EncodeVariantBits(bucket, doubleSided);

			const uint8_t bucketOrder = BucketSortOrder(bucket);
			const uint16_t variantBits16 = static_cast<uint16_t>(variantBits & 0xFFFFull);
			const uint32_t materialKey = static_cast<uint32_t>(instance.m_MaterialId.IsValid() ? instance.m_MaterialId.Value() : 0);
			const uint32_t meshKey = static_cast<uint32_t>(instance.m_MeshId.IsValid() ? instance.m_MeshId.Value() : 0);
			uint64_t sortKey = PackSortKey(bucketOrder, variantBits16, materialKey, meshKey);

			DrawItem drawItem{};
			drawItem.m_MeshId = instance.m_MeshId;
			drawItem.m_MaterialId = instance.m_MaterialId;
			drawItem.m_ObjectOffset = instance.m_ObjectOffset;
			drawItem.m_Bucket = bucket;
			drawItem.m_VariantBits = variantBits;
			drawItem.m_SortKey = sortKey;

			renderQueue.m_DrawItems.push_back(drawItem);
		}

		// Sort draw items by sort key
		std::stable_sort(renderQueue.m_DrawItems.begin(), renderQueue.m_DrawItems.end(),
			[](const DrawItem& a, const DrawItem& b)
			{
				const uint8_t aBucketOrder = BucketSortOrder(a.m_Bucket);
				const uint8_t bBucketOrder = BucketSortOrder(b.m_Bucket);
				if (aBucketOrder != bBucketOrder)
				{
					return aBucketOrder < bBucketOrder;
				}

				// TODO: transparent sorting by depth
				if (a.m_Bucket == RenderBucket::Transparent)
				{
					return false;
				}

				return a.m_SortKey < b.m_SortKey;
			});

		// Build draw ranges
		uint32_t currentIndex = 0;
		for (size_t bucketIndex = 0; bucketIndex < utils::ToIndex(RenderBucket::Count); ++bucketIndex)
		{
			RenderBucket bucket = static_cast<RenderBucket>(bucketIndex);
			DrawItemsRange& drawRange = renderQueue.m_BucketDrawRanges[bucketIndex];
			drawRange.m_Start = currentIndex;
			drawRange.m_Count = 0;
			while (currentIndex < renderQueue.m_DrawItems.size() &&
				renderQueue.m_DrawItems[currentIndex].m_Bucket == bucket)
			{
				++drawRange.m_Count;
				++currentIndex;
			}
		}

		return renderQueue;
	}

	constexpr uint8_t RenderQueueBuilder::BucketSortOrder(RenderBucket bucket) noexcept
	{
		return static_cast<uint8_t>(utils::ToIndexChecked(bucket));
	}

	constexpr uint64_t RenderQueueBuilder::EncodeVariantBits(RenderBucket bucket, bool doubleSided) noexcept
	{
		const auto encodeBucket = [](RenderBucket bucket)
			{
				uint64_t value = utils::ToIndexChecked(bucket);
				return (value << VariantBit::BucketShift) & VariantBit::BucketMask;
			};

		const auto encodeDoubleSided = [](bool doubleSided)
			{
				return doubleSided ? VariantBit::DoubleSided : 0ull;
			};

		return encodeBucket(bucket) | encodeDoubleSided(doubleSided);
	}

	// Pack sort key as:
	// [8 bits]  Bucket Order
	// [8 bits]  Variant Bits
	// [24 bits]  Material Key
	// [24 bits]  Mesh Key
	constexpr uint64_t RenderQueueBuilder::PackSortKey(uint8_t bucketOrder,
		uint16_t variantBits, uint32_t materialKey, uint32_t meshKey) noexcept
	{
		const uint64_t bucketPart = (static_cast<uint64_t>(bucketOrder) & 0xFFull) << 56;
		const uint64_t variantPart = (static_cast<uint64_t>(variantBits) & 0xFFull) << 48;
		const uint64_t materialPart = (static_cast<uint64_t>(materialKey) & 0xFFFFFFull) << 24;
		const uint64_t meshPart = static_cast<uint64_t>(meshKey) & 0xFFFFFFull;

		return bucketPart | variantPart | materialPart | meshPart;
	}

	RenderBucket RenderQueueBuilder::DecideRenderBucket(const Material* material) noexcept
	{
		if (!material)
		{
			return RenderBucket::Opaque;
		}

		switch (material->m_AlphaMode)
		{
		case AlphaMode::Mask:
			return RenderBucket::AlphaTest;
		case AlphaMode::Blend:
			return RenderBucket::Transparent;
		case AlphaMode::Opaque:
		default:
			return RenderBucket::Opaque;
		}
	}

	bool RenderQueueBuilder::IsDoubleSided(const Material* material) noexcept
	{
		if (!material)
		{
			return false;
		}
		return (material->m_Flags & MaterialFlags::DoubleSided) != MaterialFlags::None;
	}
}