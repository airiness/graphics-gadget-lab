#pragma once
#include "VertexData.h"

namespace gglab
{
	namespace primitive
	{
		class Cube
		{
		public:
			static entt::entity Create() noexcept;

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