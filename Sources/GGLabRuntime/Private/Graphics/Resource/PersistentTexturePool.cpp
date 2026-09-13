#include "Graphics/Resource/PersistentTexturePool.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	uint64_t EstimatePersistentTextureBytes(const RHITextureDesc& desc) noexcept
	{
		const RHIFormatInfo& formatInfo = GetRHIFormatInfo(desc.m_Format);
		if (formatInfo.m_BytesPerBlock == 0 || formatInfo.m_BlockWidth == 0 ||
			formatInfo.m_BlockHeight == 0)
		{
			return 0;
		}

		uint64_t bytes = 0;
		for (uint32_t mip = 0; mip < desc.m_MipLevels; ++mip)
		{
			const uint64_t width = std::max<uint32_t>(1, desc.m_Extent.m_Width >> mip);
			const uint64_t height = std::max<uint32_t>(1, desc.m_Extent.m_Height >> mip);
			const uint64_t depth = std::max<uint32_t>(1, desc.m_Extent.m_Depth >> mip);
			const uint64_t blockColumns =
				(width + formatInfo.m_BlockWidth - 1) / formatInfo.m_BlockWidth;
			const uint64_t blockRows =
				(height + formatInfo.m_BlockHeight - 1) / formatInfo.m_BlockHeight;
			const uint64_t mipBytes = utils::SaturatingMultiply(utils::SaturatingMultiply(
				utils::SaturatingMultiply(blockColumns, blockRows), depth),
				static_cast<uint64_t>(formatInfo.m_BytesPerBlock));
			bytes = utils::SaturatingAdd(bytes, mipBytes);
		}

		return utils::SaturatingMultiply(
			utils::SaturatingMultiply(bytes, static_cast<uint64_t>(desc.m_ArraySize)),
			static_cast<uint64_t>(desc.m_SampleCount));
	}

	PersistentTextureAllocation::PersistentTextureAllocation(PersistentTexturePool* pool,
		uint64_t allocationId, RHITextureHandle texture,
		const RHIOwnedTextureCreateInfo& createInfo, uint64_t estimatedBytes) noexcept :
		m_Pool(pool), m_AllocationId(allocationId), m_Texture(texture), m_CreateInfo(createInfo),
		m_EstimatedBytes(estimatedBytes)
	{
	}

	PersistentTextureAllocation::~PersistentTextureAllocation() noexcept
	{
		GGLAB_ASSERT_MSG(!IsValid(),
			"Persistent texture allocations must be explicitly released before destruction.");
	}

	PersistentTextureAllocation::PersistentTextureAllocation(
		PersistentTextureAllocation&& rhs) noexcept
	{
		*this = std::move(rhs);
	}

	PersistentTextureAllocation& PersistentTextureAllocation::operator=(
		PersistentTextureAllocation&& rhs) noexcept
	{
		if (this == &rhs)
		{
			return *this;
		}

		GGLAB_ASSERT_MSG(!IsValid(),
			"Move assignment must not orphan an active persistent texture allocation.");
		if (IsValid())
		{
			return *this;
		}

		m_Pool = std::exchange(rhs.m_Pool, nullptr);
		m_AllocationId = std::exchange(rhs.m_AllocationId, 0);
		m_Texture = std::exchange(rhs.m_Texture, {});
		m_CreateInfo = rhs.m_CreateInfo;
		m_EstimatedBytes = std::exchange(rhs.m_EstimatedBytes, 0);
		rhs.m_CreateInfo = {};
		return *this;
	}

	bool PersistentTextureAllocation::IsValid() const noexcept
	{
		return m_Pool != nullptr && m_AllocationId != 0 && m_Texture.IsValid();
	}

	void PersistentTextureAllocation::Reset() noexcept
	{
		m_Pool = nullptr;
		m_AllocationId = 0;
		m_Texture.Reset();
		m_CreateInfo = {};
		m_EstimatedBytes = 0;
	}

	PersistentTexturePool::PersistentTexturePool(RHIDevice* device) noexcept : m_Device(device)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "PersistentTexturePool requires an RHI device.");
	}

	PersistentTexturePool::~PersistentTexturePool() noexcept
	{
		Tick();
		GGLAB_ASSERT_MSG(m_ActiveTextures.empty(),
			"PersistentTexturePool destroyed with active texture allocations.");
		GGLAB_ASSERT_MSG(m_PendingRetirements.empty(),
			"PersistentTexturePool destroyed before pending texture retirements completed.");
	}

	PersistentTextureAllocation PersistentTexturePool::AcquireTexture(
		const RHIOwnedTextureCreateInfo& createInfo, std::string_view logicalName) noexcept
	{
		const RHITextureValidationResult validation = ValidateRHITextureDesc(createInfo.m_Desc);
		if (!validation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"PersistentTexturePool rejected an invalid texture description (reason={}).",
				RHITextureValidationErrorText(validation.m_Error));
			return {};
		}

		const uint64_t allocationId = m_NextAllocationId++;
		GGLAB_ASSERT_MSG(allocationId != 0,
			"Persistent texture allocation identity overflowed its valid range.");
		if (allocationId == 0)
		{
			return {};
		}

		const std::string resolvedName = !logicalName.empty()
			? std::string(logicalName)
			: createInfo.m_Desc.m_DebugName ? std::string(createInfo.m_Desc.m_DebugName)
										  : std::string("Unnamed");
		const RHIResourceDebugIdentityDesc debugIdentity{
			.m_Domain = RHIResourceDebugDomain::Renderer,
			.m_Category = "PersistentTexture",
			.m_Label = resolvedName,
			.m_StableId = allocationId,
		};
		const RHITextureHandle texture = m_Device->CreateTexture(createInfo, debugIdentity);
		if (!texture.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"PersistentTexturePool failed to create texture allocation {}.", allocationId);
			return {};
		}

		m_Device->SetTextureDebugBinding(texture, {
			.m_Owner = resolvedName,
			.m_Serial = allocationId,
			.m_Mode = RHIResourceDebugBindingMode::Exclusive,
			});

		RHIOwnedTextureCreateInfo storedCreateInfo = createInfo;
		storedCreateInfo.m_Desc.m_DebugName = nullptr;
		const uint64_t estimatedBytes = EstimatePersistentTextureBytes(storedCreateInfo.m_Desc);
		m_ActiveTextures.emplace(allocationId, ActiveTextureRecord{
			.m_Texture = RHITextureOwner(m_Device, texture),
			.m_CreateInfo = storedCreateInfo,
			.m_EstimatedBytes = estimatedBytes,
			.m_LogicalName = resolvedName,
			});
		++m_TotalAcquireCount;

		return PersistentTextureAllocation(
			this, allocationId, texture, storedCreateInfo, estimatedBytes);
	}

	bool PersistentTexturePool::ReleaseTexture(PersistentTextureAllocation&& allocation,
		const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocation.IsValid() || allocation.m_Pool != this || !fencePoint.IsValid())
		{
			++m_RejectedReleaseCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"PersistentTexturePool rejected an invalid texture release.");
			return false;
		}

		const auto active = m_ActiveTextures.find(allocation.m_AllocationId);
		if (active == m_ActiveTextures.end() ||
			active->second.m_Texture.Get() != allocation.m_Texture)
		{
			++m_RejectedReleaseCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"PersistentTexturePool rejected a stale or foreign texture allocation.");
			return false;
		}

		m_Device->RecordTextureUse(allocation.m_Texture, fencePoint);
		ActiveTextureRecord record = std::move(active->second);
		m_ActiveTextures.erase(active);
		m_PendingRetirements.push_back({
			.m_AllocationId = allocation.m_AllocationId,
			.m_Texture = std::move(record.m_Texture),
			.m_EstimatedBytes = record.m_EstimatedBytes,
			.m_FencePoint = fencePoint,
			.m_LogicalName = std::move(record.m_LogicalName),
			});
		allocation.Reset();
		++m_TotalReleaseCount;
		return true;
	}

	bool PersistentTexturePool::ReleaseTextureWithoutSubmission(
		PersistentTextureAllocation&& allocation) noexcept
	{
		if (!allocation.IsValid() || allocation.m_Pool != this)
		{
			++m_RejectedReleaseCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"PersistentTexturePool rejected an invalid unsubmitted texture release.");
			return false;
		}

		const auto active = m_ActiveTextures.find(allocation.m_AllocationId);
		if (active == m_ActiveTextures.end() ||
			active->second.m_Texture.Get() != allocation.m_Texture)
		{
			++m_RejectedReleaseCount;
			GGLAB_LOG_GRAPHICS_WARN(
				"PersistentTexturePool rejected a stale or foreign unsubmitted allocation.");
			return false;
		}

		ActiveTextureRecord record = std::move(active->second);
		m_ActiveTextures.erase(active);
		allocation.Reset();
		++m_TotalReleaseCount;
		++m_CompletedRetirementCount;
		return true;
	}

	void PersistentTexturePool::Tick() noexcept
	{
		std::erase_if(m_PendingRetirements,
			[this](const PendingTextureRetirement& pending)
			{
				if (!m_Device->IsFencePointCompleted(pending.m_FencePoint))
				{
					return false;
				}
				++m_CompletedRetirementCount;
				return true;
			});
	}

	PersistentTexturePoolDiagnostics PersistentTexturePool::GetDiagnostics() const
	{
		PersistentTexturePoolDiagnostics diagnostics{
			.m_ActiveTextureCount = static_cast<uint32_t>(m_ActiveTextures.size()),
			.m_PendingRetirementTextureCount =
				static_cast<uint32_t>(m_PendingRetirements.size()),
			.m_TotalAcquireCount = m_TotalAcquireCount,
			.m_TotalReleaseCount = m_TotalReleaseCount,
			.m_CompletedRetirementCount = m_CompletedRetirementCount,
			.m_RejectedReleaseCount = m_RejectedReleaseCount,
		};
		for (const auto& [allocationId, active] : m_ActiveTextures)
		{
			GGLAB_UNUSED(allocationId);
			diagnostics.m_EstimatedActiveBytes += active.m_EstimatedBytes;
		}
		diagnostics.m_PendingRetirements.reserve(m_PendingRetirements.size());
		for (const PendingTextureRetirement& pending : m_PendingRetirements)
		{
			diagnostics.m_EstimatedPendingRetirementBytes += pending.m_EstimatedBytes;
			diagnostics.m_PendingRetirements.push_back({
				.m_AllocationId = pending.m_AllocationId,
				.m_Texture = pending.m_Texture.Get(),
				.m_EstimatedBytes = pending.m_EstimatedBytes,
				.m_FencePoint = pending.m_FencePoint,
				.m_LogicalName = pending.m_LogicalName,
				});
		}
		return diagnostics;
	}
}
