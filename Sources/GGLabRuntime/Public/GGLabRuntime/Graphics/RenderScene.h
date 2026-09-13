#pragma once
#include "GGLabRuntime/Core/Math/BoundingVolumes.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace gglab
{
	struct RenderInstance
	{
		MeshID m_MeshId{};
		RenderMaterialKey m_MaterialKey{};
		MaterialFlags m_MaterialFlags = MaterialFlags::None;
		AlphaMode m_AlphaMode = AlphaMode::Opaque;

		uint32_t m_ObjectOffset = 0;
		uint32_t m_MaterialOffset = 0;

		Vector3 m_WorldCenterPos = Vector3::Zero;
		math::Sphere m_WorldBounds{};
		bool m_HasWorldBounds = false;
	};

	struct RenderScene
	{
		uint32_t m_ObjectBaseIndex = 0;
		uint32_t m_ObjectCount = 0;

		uint32_t m_MaterialBaseIndex = 0;
		uint32_t m_MaterialCount = 0;

		uint32_t m_ViewBaseIndex = 0;
		uint32_t m_ViewCount = 0;

		uint32_t m_LightBaseIndex = 0;
		uint32_t m_LightCount = 0;
		uint32_t m_DirectionalShadowLightIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_DirectionalLightCount = 0;
		uint32_t m_LocalLightCount = 0;
		std::array<uint32_t, MaxLightCapacity> m_LightTypesByIndex{};
		std::vector<uint32_t> m_GlobalLightIndices;

		uint64_t m_SceneConstantBufferOffset = 0;

		std::vector<RenderInstance> m_RenderInstances;
	};
}
