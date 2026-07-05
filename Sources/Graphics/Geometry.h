#pragma once
#include "Graphics/VertexData.h"
#include "Scene/Components.h"

#include <optional>

namespace gglab
{
	class AssetManager;
	class SamplerRegistry;
	class World;

	namespace primitive
	{
		class PrimitiveBase
		{
		public:
			struct CreateInfo
			{
				AssetManager* m_AssetManager = nullptr;
				SamplerRegistry* m_SamplerRegistry = nullptr;
				World* m_World = nullptr;
				components::TransformComponent m_Transform{};
				std::optional<components::MaterialInstanceComponent> m_MaterialInstance;
			};

		protected:
			using VertexBuilder = std::vector<Vertex>(*)() noexcept;
			using IndexBuilder = std::vector<uint32_t>(*)() noexcept;

			static entt::entity CreatePrimitive(
				const CreateInfo& info,
				ModelID modelId,
				MeshID meshId,
				std::string_view name,
				VertexBuilder buildVertices,
				IndexBuilder buildIndices) noexcept;
		};

		class Cube final : public PrimitiveBase
		{
		public:
			static entt::entity Create(const CreateInfo& info) noexcept;

		private:
			static std::vector<Vertex> GetVerticesData() noexcept;
			static std::vector<uint32_t> GetIndicesData() noexcept;

			static constexpr uint32_t FaceCount = 6;
			static constexpr uint32_t VertexCountPerFace = 4;
		};

		class Sphere final : public PrimitiveBase
		{
		public:
			static entt::entity Create(const CreateInfo& info) noexcept;

		private:
			static std::vector<Vertex> GetVerticesData() noexcept;
			static std::vector<uint32_t> GetIndicesData() noexcept;

			static constexpr uint32_t SliceCount = 32;
			static constexpr uint32_t StackCount = 16;
		};
	}
}
