#pragma once

#include "GGLabRuntime/Scene/Components.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	struct EntityToolingTarget
	{
		uint64_t m_WorldId = 0;
		uint32_t m_EntityId = 0;
		bool operator==(const EntityToolingTarget&) const noexcept = default;
	};

	// Public component contracts are copied values, never borrowed ECS storage.
	struct EntityToolingObservation
	{
		EntityToolingTarget m_Target;
		std::optional<components::TransformComponent> m_Transform;
		std::optional<components::LightComponent> m_Light;
		std::optional<components::ModelComponent> m_Model;
	};

	struct WorldToolingSnapshot
	{
		uint64_t m_WorldId = 0;
		std::vector<EntityToolingObservation> m_Entities;
		[[nodiscard]] const EntityToolingObservation* FindEntity(EntityToolingTarget target) const noexcept
		{
			if (target.m_WorldId != m_WorldId || m_WorldId == 0) return nullptr;
			for (const auto& entity : m_Entities)
			{
				if (entity.m_Target == target) return &entity;
			}
			return nullptr;
		}
	};

	// Borrowed only for synchronous owner-thread tooling draw. Values own no entity lifetime.
	// Targets carry World identity and the full versioned entity ID; commands revalidate both.
	class WorldToolingViewBase
	{
	public:
		virtual ~WorldToolingViewBase() = default;
		[[nodiscard]] virtual WorldToolingSnapshot GetEntities() const = 0;
	};

	class WorldToolingControlBase
	{
	public:
		virtual ~WorldToolingControlBase() = default;
		[[nodiscard]] virtual EntityToolingTarget CreateEntity() = 0;
		virtual bool DestroyEntity(EntityToolingTarget target) = 0;
		virtual bool AddTransform(EntityToolingTarget target) = 0;
		virtual bool AddLight(EntityToolingTarget target) = 0;
		virtual bool AddModel(EntityToolingTarget target, ModelID model) = 0;
		virtual bool SetTransform(EntityToolingTarget target, const components::TransformComponent& value) = 0;
		virtual bool SetLight(EntityToolingTarget target, const components::LightComponent& value) = 0;
	};
}
