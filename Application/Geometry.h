#pragma once
#include "VertexData.h"

namespace gglab
{
	class AssetManager;
	class World;

	namespace primitive
	{
		class Cube
		{
		public:
			struct CreateInfo
			{
				AssetManager* m_AssetManager = nullptr;
				World* m_World = nullptr;
			};

		public:
			static entt::entity Create(const CreateInfo& info) noexcept;

		private:
			static std::vector<Vertex> GetVerticesData() noexcept;
			static std::vector<uint32_t> GetIndicesData() noexcept;

		private:
			static constexpr uint32_t FaceCount = 6;
			static constexpr uint32_t VertexCountPerFace = 4;

			static const char* const TextPath;
		};
	}
}