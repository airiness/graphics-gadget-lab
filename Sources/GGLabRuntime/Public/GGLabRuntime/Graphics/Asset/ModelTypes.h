#pragma once
#include "GGLabFoundation/Async/ProgressChannel.h"
#include "GGLabRuntime/Core/Math/BoundingVolumes.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/StringId.h"
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "GGLabRuntime/Graphics/Asset/AssetLifecycleTypes.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

namespace gglab
{
	enum class ModelType : uint32_t
	{
		Invalid,
		GlTF,
		Procedural,
	};

	struct Mesh : AssetLifecycle
	{
		MeshID m_Id{};
		bool m_IsUploaded = false;
		bool m_HasBounds = false;
		bool m_CancelRequested = false;
		bool m_IsReloading = false;
		ModelID m_SourceModelId{};
		uint32_t m_SourceMeshIndex = std::numeric_limits<uint32_t>::max();

		StringID m_Name{};
		ProgressChannelPtr m_LoadProgress;

		RHIBufferOwner m_VertexBuffer;
		RHIBufferOwner m_IndexBuffer;

		RHIVertexBufferBinding m_VertexBufferBinding{};
		RHIIndexBufferBinding m_IndexBufferBinding{};

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		math::Sphere m_Sphere{};
		math::Aabb m_Aabb{};
	};

	struct ModelMesh
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
		Matrix m_LocalTransform = Matrix::Identity;
	};

	struct Model : AssetLifecycle
	{
		ModelID m_Id{};
		StringID m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::filesystem::path m_SourcePath;
		ArtifactContentDigest m_ImportArtifactContentDigest{};
		ProgressChannelPtr m_LoadProgress;
		bool m_CancelRequested = false;
		std::vector<ModelMesh> m_MeshInstance;
	};
}
