#include "Precompiled.h"
#include "RGExternalResourceRegistry.h"
#include "DX12ViewCache.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"

namespace gglab
{
	RGExternalResourceRegistry::RGExternalResourceRegistry(DX12ViewCache* viewCache) noexcept :
		m_ViewCache(viewCache)
	{
	}

	RGExternalResourceRegistry::~RGExternalResourceRegistry()
	{
		// Make sure destruct before ViewCache
		Clear(true);
	}

	ResourceIndex RGExternalResourceRegistry::GetOrCreate(const DX12Texture* texture) noexcept
	{
		GGLAB_ASSERT_MSG(texture != nullptr, "RGExternalResourceRegistry::GetOrCreate(texture): texture is null.");
		return GetOrCreateImpl(texture->Get(), ExternalResourceIndex::Type::Texture);
	}

	ResourceIndex RGExternalResourceRegistry::GetOrCreate(const DX12Buffer* buffer) noexcept
	{
		GGLAB_ASSERT_MSG(buffer != nullptr, "RGExternalResourceRegistry::GetOrCreate(buffer): buffer is null.");
		return GetOrCreateImpl(buffer->Get(), ExternalResourceIndex::Type::Buffer);
	}

	std::optional<ResourceIndex> RGExternalResourceRegistry::TryGet(const DX12Texture* texture) const noexcept
	{
		if (!texture)
		{
			return std::nullopt;
		}

		auto indexOpt = TryGetImpl(texture->Get());
		if (!indexOpt)
		{
			return std::nullopt;
		}

		GGLAB_ASSERT_MSG(
			ExternalResourceIndex::IsExternal(*indexOpt) &&
			ExternalResourceIndex::GetType(*indexOpt) == ExternalResourceIndex::Type::Texture,
			"RGExternalResourceRegistry::TryGet(texture): type mismatch or non-external index.");

		return indexOpt;
	}

	std::optional<ResourceIndex> RGExternalResourceRegistry::TryGet(const DX12Buffer* buffer) const noexcept
	{
		if (!buffer)
		{
			return std::nullopt;
		}

		auto indexOpt = TryGetImpl(buffer->Get());
		if (!indexOpt)
		{
			return std::nullopt;
		}

		GGLAB_ASSERT_MSG(
			ExternalResourceIndex::IsExternal(*indexOpt) &&
			ExternalResourceIndex::GetType(*indexOpt) == ExternalResourceIndex::Type::Buffer,
			"RGExternalResourceRegistry::TryGet(buffer): type mismatch or non-external index.");

		return indexOpt;
	}

	void RGExternalResourceRegistry::Forget(const DX12Texture* texture,
		bool freeViewsImmediately,
		const DX12FencePoint* fencePointOpt) noexcept
	{
		if (!texture)
		{
			return;
		}

		ForgetTexture(texture->Get(), freeViewsImmediately, fencePointOpt);
	}

	void RGExternalResourceRegistry::Forget(const DX12Buffer* buffer) noexcept
	{
		if (!buffer)
		{
			return;
		}

		ForgetBuffer(buffer->Get());
	}

	void RGExternalResourceRegistry::Clear(bool freeViewsImmediately) noexcept
	{
		std::vector<ResourceIndex> textureIndices;

		{
			std::unique_lock lock(m_Mutex);

			if (m_ViewCache && freeViewsImmediately)
			{
				textureIndices.reserve(m_Table.size());
				for (const auto& [resource, index] : m_Table)
				{
					if (ExternalResourceIndex::IsExternal(index) &&
						ExternalResourceIndex::GetType(index) == ExternalResourceIndex::Type::Texture)
					{
						textureIndices.push_back(index);
					}
				}
			}

			m_Table.clear();

		}

		if (m_ViewCache && freeViewsImmediately)
		{
			for (auto index : textureIndices)
			{
				m_ViewCache->FreeAllImmediately(index);
			}
		}
	}

	ResourceIndex RGExternalResourceRegistry::GetOrCreateImpl(const ID3D12Resource* resource, ExternalResourceIndex::Type type) noexcept
	{
		GGLAB_ASSERT_MSG(resource != nullptr, "RGExternalResourceRegistry::GetOrCreate: resource is null.");

		{
			std::shared_lock lock(m_Mutex);
			if (auto iter = m_Table.find(resource); iter != m_Table.end())
			{
				const ResourceIndex index = iter->second;

				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(index),
					"RGExternalResourceRegistry: stored index is not external.");

				GGLAB_ASSERT_MSG(ExternalResourceIndex::GetType(index) == type,
					"RGExternalResourceRegistry: same ID3D12Resource registered with a different type.");

				return index;
			}
		}

		{
			std::unique_lock lock(m_Mutex);
			if (auto iter = m_Table.find(resource); iter != m_Table.end())
			{
				const ResourceIndex index = iter->second;

				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(index),
					"RGExternalResourceRegistry: stored index is not external.");

				GGLAB_ASSERT_MSG(ExternalResourceIndex::GetType(index) == type,
					"RGExternalResourceRegistry: same ID3D12Resource registered with a different type.");

				return index;
			}

			GGLAB_ASSERT_MSG(m_NextId != 0, "RGExternalResourceRegistry: id wrapped around to 0.");
			GGLAB_ASSERT_MSG((m_NextId & ~ExternalResourceIndex::IdMask) == 0,
				"RGExternalResourceRegistry: external id overflowed.");

			const auto id = m_NextId++;
			const ResourceIndex index = ExternalResourceIndex::MakeIndex(type, id);

			m_Table.emplace(resource, index);
			return index;
		}
	}

	std::optional<ResourceIndex> RGExternalResourceRegistry::TryGetImpl(const ID3D12Resource* resource) const noexcept
	{
		if (!resource)
		{
			return std::nullopt;
		}

		std::shared_lock lock(m_Mutex);
		if (auto iter = m_Table.find(resource); iter != m_Table.end())
		{
			return iter->second;
		}

		return std::nullopt;
	}

	void RGExternalResourceRegistry::ForgetTexture(const ID3D12Resource* resource,
		bool freeViewsImmediately,
		const DX12FencePoint* fencePointOpt) noexcept
	{
		if (!resource)
		{
			return;
		}

		// Remove from table under lock, capture needs
		ResourceIndex index{};
		bool needFreeImmediately = false;
		bool needRetire = false;
		DX12FencePoint fencePointCopy{};

		{
			std::unique_lock lock(m_Mutex);

			auto iter = m_Table.find(resource);
			if (iter == m_Table.end())
			{
				return;
			}

			index = iter->second;

			GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(index),
				"RGExternalResourceRegistry::ForgetTexture: stored index is not external.");

			GGLAB_ASSERT_MSG(ExternalResourceIndex::GetType(index) == ExternalResourceIndex::Type::Texture,
				"ForgetTexture called on non-texture external entry.");

			m_Table.erase(iter);

			if (m_ViewCache)
			{
				if (freeViewsImmediately)
				{
					needFreeImmediately = true;
				}
				else
				{
					GGLAB_ASSERT_MSG(fencePointOpt != nullptr,
						"ForgetTexture: fencePointOpt is required when not freeing immediately.");
					fencePointCopy = *fencePointOpt;
					needRetire = true;
				}
			}
		}

		// Call ViewCache outside registry lock to avoid deadlocks
		if (m_ViewCache)
		{
			if (needFreeImmediately)
			{
				m_ViewCache->FreeAllImmediately(index);
			}
			else if (needRetire)
			{
				m_ViewCache->RetireResourceAllViews(index, fencePointCopy);
			}
		}
	}

	void RGExternalResourceRegistry::ForgetBuffer(const ID3D12Resource* resource) noexcept
	{
		if (!resource)
		{
			return;
		}

		std::unique_lock lock(m_Mutex);

		auto it = m_Table.find(resource);
		if (it == m_Table.end())
		{
			return;
		}

		const ResourceIndex index = it->second;

		GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(index),
			"RGExternalResourceRegistry::ForgetBuffer: stored index is not external.");

		GGLAB_ASSERT_MSG(ExternalResourceIndex::GetType(index) == ExternalResourceIndex::Type::Buffer,
			"ForgetBuffer called on non-buffer external entry.");

		m_Table.erase(it);
	}
}