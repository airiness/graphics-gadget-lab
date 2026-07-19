#pragma once
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/Loading/ModelImporter.h"

#include <memory>

namespace gglab
{
	struct ModelImportArtifact
	{
		ImportedModel m_Model;
		ArtifactContentDigest m_ContentDigest{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_Model.m_Meshes.empty() && m_ContentDigest.IsValid();
		}

		[[nodiscard]] uint64_t GetAllocatedBytes() const noexcept;
	};

	using ModelImportArtifactHandle = std::shared_ptr<const ModelImportArtifact>;

	[[nodiscard]] ArtifactContentDigest ComputeModelImportArtifactContentDigest(
		const ImportedModel& model) noexcept;
	[[nodiscard]] ModelImportArtifactHandle CreateModelImportArtifact(
		ImportedModel&& model) noexcept;
}
