#include "Core/Precompiled.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/AssetManager.h"

namespace gglab
{
	RenderQueue RenderQueueBuilder::Build(const BuildInfo& info) noexcept
	{
		RenderQueue renderQueue{};
		renderQueue.m_ViewId = info.m_RenderView.m_ViewId;

		const auto& instances = info.m_RenderScene.m_RenderInstances;
		renderQueue.m_Statistics.m_TotalInstanceCount = static_cast<uint32_t>(instances.size());
		renderQueue.m_DrawItems.reserve(instances.size());

		for (const auto& instance : instances)
		{
			if (instance.m_HasWorldBounds)
			{
				bool visible = true;
				for (const math::Frustum& frustum : info.m_CullingFrustums)
				{
					if (!math::Intersects(frustum, instance.m_WorldBounds))
					{
						visible = false;
						break;
					}
				}
				if (!visible)
				{
					++renderQueue.m_Statistics.m_CulledInstanceCount;
					continue;
				}
			}
			else
			{
				++renderQueue.m_Statistics.m_UnboundedInstanceCount;
			}

			const auto* mesh = info.m_AssetManager.GetMesh(instance.m_MeshId);
			if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
			{
				++renderQueue.m_Statistics.m_InvalidInstanceCount;
				continue;
			}

			RenderBucket bucket = DecideRenderBucket(instance.m_AlphaMode);
			bool doubleSided = IsDoubleSided(instance.m_MaterialFlags);

			uint64_t variantBits = 0;
			variantBits |= EncodeVariantBits(bucket, doubleSided);

			const uint8_t bucketOrder = BucketSortOrder(bucket);
			const uint8_t variantBits8 = static_cast<uint8_t>(variantBits & VariantMask);
			const uint64_t materialValue = instance.m_MaterialKey.m_Value ^
				(static_cast<uint64_t>(instance.m_MaterialKey.m_Domain) << 63);
			const uint32_t materialKey = static_cast<uint32_t>(
				materialValue ^ (materialValue >> 32));
			const uint32_t meshKey = static_cast<uint32_t>(instance.m_MeshId.IsValid() ? instance.m_MeshId.Value() : 0);
			uint64_t sortKey = PackSortKey(bucketOrder, variantBits8, materialKey, meshKey);

			DrawItem drawItem{};
			drawItem.m_MeshId = instance.m_MeshId;
			drawItem.m_MaterialKey = instance.m_MaterialKey;
			drawItem.m_ObjectOffset = instance.m_ObjectOffset;
			drawItem.m_Bucket = bucket;
			drawItem.m_VariantBits = variantBits;
			drawItem.m_SortKey = sortKey;
			drawItem.mDepthKey = MakeDepthKey(info.m_RenderView, instance.m_WorldCenterPos);

			renderQueue.m_DrawItems.push_back(drawItem);
			++renderQueue.m_Statistics.m_VisibleInstanceCount;
		}
		renderQueue.m_Statistics.m_DrawItemCount = static_cast<uint32_t>(renderQueue.m_DrawItems.size());

		// Sort draw items by sort key
		std::stable_sort(renderQueue.m_DrawItems.begin(), renderQueue.m_DrawItems.end(),
			[](const DrawItem& a, const DrawItem& b)
			{
				if (a.m_Bucket != b.m_Bucket)
				{
					return a.m_Bucket < b.m_Bucket;
				}

				// Transparent sorting by depth
				if (a.m_Bucket == RenderBucket::Transparent)
				{
					if (a.mDepthKey != b.mDepthKey)
					{
						return a.mDepthKey < b.mDepthKey;
					}
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
		uint8_t variantBits, uint32_t materialKey, uint32_t meshKey) noexcept
	{
		const uint64_t bucketPart = (static_cast<uint64_t>(bucketOrder) & 0xFFull) << 56;
		const uint64_t variantPart = (static_cast<uint64_t>(variantBits) & VariantMask) << 48;
		const uint64_t materialPart = (static_cast<uint64_t>(materialKey) & 0xFFFFFFull) << 24;
		const uint64_t meshPart = static_cast<uint64_t>(meshKey) & 0xFFFFFFull;

		return bucketPart | variantPart | materialPart | meshPart;
	}

	RenderBucket RenderQueueBuilder::DecideRenderBucket(AlphaMode alphaMode) noexcept
	{
		switch (alphaMode)
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

	bool RenderQueueBuilder::IsDoubleSided(MaterialFlags flags) noexcept
	{
		return (flags & MaterialFlags::DoubleSided) != MaterialFlags::None;
	}

	uint32_t RenderQueueBuilder::MakeDepthKey(const RenderView& renderView, const Vector3& worldCenterPos) noexcept
	{
		const Vector3 centerViewPos = math::TransformPoint(worldCenterPos, renderView.m_View);

		const float nearZ = renderView.m_Near;
		const float farZ = renderView.m_Far;

		const float denom = farZ - nearZ;
		if (denom <= 0.0f)
		{
			// Invalid near/far plane, return 0 depth key.
			return 0u;
		}

		// Clamp Z to the valid [near, far] range from the view.
		float z = centerViewPos.m_Z;
		z = std::clamp(z, nearZ, farZ);

		float t = (z - nearZ) / denom;
		t = std::clamp(t, 0.0f, 1.0f);

		constexpr uint32_t Max = (1u << 24) - 1;

		// far -> 0, near -> Max, so ascending sort = back-to-front
		const float inv = 1.0f - t;
		const uint32_t key = static_cast<uint32_t>(inv * static_cast<float>(Max) + 0.5f);
		return (key > Max) ? Max : key;
	}
}
