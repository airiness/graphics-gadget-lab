#include "Core/Precompiled.h"
#include "Graphics/Asset/Store/ModelStore.h"

namespace gglab
{
	const Model* ModelStore::Find(ModelID modelId) const noexcept
	{
		const auto iterator = m_Entries.find(modelId);
		return iterator != m_Entries.end() ? iterator->second.get() : nullptr;
	}

	Model* ModelStore::Edit(ModelID modelId) noexcept
	{
		return const_cast<Model*>(std::as_const(*this).Find(modelId));
	}

	ModelID ModelStore::FindByPath(const std::filesystem::path& canonicalPath) const noexcept
	{
		const auto iterator = m_PathIndex.find(canonicalPath);
		return iterator != m_PathIndex.end() ? iterator->second : ModelID{};
	}

	ModelID ModelStore::Create(const std::filesystem::path& canonicalPath) noexcept
	{
		GGLAB_ASSERT_MSG(
			!canonicalPath.empty(), "A stored model path must be canonical and non-empty.");
		if (canonicalPath.empty() || m_PathIndex.contains(canonicalPath))
		{
			GGLAB_ASSERT_MSG(false, "A model path may only have one active store entry.");
			return {};
		}

		const ModelID modelId = m_IdCounter.Acquire();
		auto model = std::make_unique<Model>();
		model->m_Id = modelId;
		const auto [pathIterator, pathInserted] = m_PathIndex.emplace(canonicalPath, modelId);
		GGLAB_ASSERT(pathInserted);
		if (!pathInserted)
		{
			return {};
		}

		const auto [entryIterator, entryInserted] = m_Entries.emplace(modelId, std::move(model));
		GGLAB_ASSERT(entryInserted);
		if (!entryInserted)
		{
			m_PathIndex.erase(pathIterator);
			return {};
		}
		return entryIterator->first;
	}

	ModelStore::InsertResult ModelStore::Insert(std::unique_ptr<Model>&& model) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(model);
		if (!model)
		{
			return {};
		}

		ModelID modelId = model->m_Id;
		if (!modelId.IsValid())
		{
			modelId = m_IdCounter.Acquire();
			model->m_Id = modelId;
		}
		if (m_Entries.contains(modelId))
		{
			return { .m_Id = modelId };
		}

		const auto [iterator, inserted] = m_Entries.emplace(modelId, std::move(model));
		GGLAB_ASSERT(inserted);
		return {
			.m_Id = inserted ? iterator->first : ModelID{},
			.m_Inserted = inserted,
		};
	}

	bool ModelStore::DetachPath(
		const std::filesystem::path& canonicalPath, ModelID modelId) noexcept
	{
		const auto iterator = m_PathIndex.find(canonicalPath);
		if (iterator == m_PathIndex.end() || iterator->second != modelId)
		{
			return false;
		}
		m_PathIndex.erase(iterator);
		return true;
	}

	bool ModelStore::Remove(ModelID modelId) noexcept
	{
		const bool removed = m_Entries.erase(modelId) > 0;
		const size_t removedPaths = std::erase_if(
			m_PathIndex, [modelId](const auto& entry) noexcept { return entry.second == modelId; });
		return removed || removedPaths > 0;
	}
}
