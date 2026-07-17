#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/GraphicsTypes.h"

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace gglab
{
	class ModelStore final
	{
	public:
		using EntryMap = std::unordered_map<ModelID, std::unique_ptr<Model>>;
		struct InsertResult
		{
			ModelID m_Id{};
			bool m_Inserted = false;
		};

		ModelStore() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(ModelStore);

		[[nodiscard]] const Model* Find(ModelID modelId) const noexcept;
		[[nodiscard]] Model* Edit(ModelID modelId) noexcept;
		[[nodiscard]] ModelID FindByPath(
			const std::filesystem::path& canonicalPath) const noexcept;
		[[nodiscard]] ModelID Create(
			const std::filesystem::path& canonicalPath) noexcept;
		[[nodiscard]] InsertResult Insert(std::unique_ptr<Model>&& model) noexcept;
		[[nodiscard]] bool DetachPath(
			const std::filesystem::path& canonicalPath,
			ModelID modelId) noexcept;

		[[nodiscard]] const EntryMap& Entries() const noexcept { return m_Entries; }

	private:
		ModelIDCounter m_IdCounter{ ReservedModelCount };
		std::unordered_map<std::filesystem::path, ModelID> m_PathIndex;
		EntryMap m_Entries;
	};
}
