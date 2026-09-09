#include "Diagnostics/WorldTooling.h"
#include "GGLabRuntime/Core/World.h"

#include <algorithm>
#include <atomic>

namespace gglab
{
	namespace
	{
		// Registry context follows registry moves, and is never an entity component.
		struct WorldToolingIdentity { uint64_t m_Value = 0; };
		std::atomic<uint64_t> s_NextWorldId{ 1 };
	}

	WorldTooling::WorldTooling(World& world) : m_World(world)
	{
		auto& context = world.GetRegistry().ctx();
		auto* identity = context.find<WorldToolingIdentity>();
		if (!identity)
		{
			identity = &context.emplace<WorldToolingIdentity>(
				s_NextWorldId.fetch_add(1, std::memory_order_relaxed));
		}
		m_WorldId = identity->m_Value;
	}

	bool WorldTooling::HasWorld() const noexcept
	{
		const auto* identity = m_World.GetRegistry().ctx().find<WorldToolingIdentity>();
		return identity && identity->m_Value == m_WorldId;
	}

	bool WorldTooling::IsValid(EntityToolingTarget target) const noexcept
	{
		return HasWorld() && target.m_WorldId == m_WorldId &&
			m_World.GetRegistry().valid(static_cast<entt::entity>(target.m_EntityId));
	}

	WorldToolingSnapshot WorldTooling::GetEntities() const
	{
		WorldToolingSnapshot result;
		if (!HasWorld()) return result;
		result.m_WorldId = m_WorldId;
		const auto& registry = m_World.GetRegistry();
		std::vector<entt::entity> entities;
		for (auto entity : registry.view<const components::TransformComponent>()) entities.push_back(entity);
		for (auto entity : registry.view<const components::LightComponent>()) entities.push_back(entity);
		for (auto entity : registry.view<const components::ModelComponent>()) entities.push_back(entity);
		std::sort(entities.begin(), entities.end(), [](auto lhs, auto rhs) {
			return entt::to_integral(lhs) < entt::to_integral(rhs);
		});
		entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
		for (auto entity : entities)
		{
			EntityToolingObservation observation;
			observation.m_Target = { m_WorldId, entt::to_integral(entity) };
			if (const auto* value = registry.try_get<components::TransformComponent>(entity))
				observation.m_Transform = *value;
			if (const auto* value = registry.try_get<components::LightComponent>(entity))
				observation.m_Light = *value;
			if (const auto* value = registry.try_get<components::ModelComponent>(entity))
				observation.m_Model = *value;
			result.m_Entities.push_back(observation);
		}
		return result;
	}

	EntityToolingTarget WorldTooling::CreateEntity()
	{
		if (!HasWorld()) return {};
		auto& registry = m_World.GetRegistry();
		const auto entity = registry.create();
		registry.emplace<components::TransformComponent>(entity);
		return { m_WorldId, entt::to_integral(entity) };
	}

	bool WorldTooling::DestroyEntity(EntityToolingTarget target)
	{
		if (!IsValid(target)) return false;
		m_World.GetRegistry().destroy(static_cast<entt::entity>(target.m_EntityId));
		return true;
	}

	bool WorldTooling::AddTransform(EntityToolingTarget target)
	{
		if (!IsValid(target)) return false;
		auto& registry = m_World.GetRegistry();
		const auto entity = static_cast<entt::entity>(target.m_EntityId);
		if (registry.all_of<components::TransformComponent>(entity)) return false;
		registry.emplace<components::TransformComponent>(entity);
		return true;
	}

	bool WorldTooling::AddLight(EntityToolingTarget target)
	{
		if (!IsValid(target)) return false;
		auto& registry = m_World.GetRegistry();
		const auto entity = static_cast<entt::entity>(target.m_EntityId);
		if (registry.all_of<components::LightComponent>(entity)) return false;
		if (!registry.all_of<components::TransformComponent>(entity)) AddTransform(target);
		components::LightComponent light;
		light.m_Type = LightType::Point;
		light.m_Intensity = 3.0f;
		light.m_Range = 15.0f;
		light.m_SpotAngle = 45.0f;
		registry.emplace<components::LightComponent>(entity, light);
		return true;
	}

	bool WorldTooling::AddModel(EntityToolingTarget target, ModelID model)
	{
		if (!IsValid(target) || !model.IsValid()) return false;
		auto& registry = m_World.GetRegistry();
		const auto entity = static_cast<entt::entity>(target.m_EntityId);
		if (registry.all_of<components::ModelComponent>(entity)) return false;
		if (!registry.all_of<components::TransformComponent>(entity)) AddTransform(target);
		registry.emplace<components::ModelComponent>(entity, model);
		return true;
	}

	bool WorldTooling::SetTransform(EntityToolingTarget target, const components::TransformComponent& value)
	{
		if (!IsValid(target)) return false;
		auto* transform = m_World.GetRegistry().try_get<components::TransformComponent>(
			static_cast<entt::entity>(target.m_EntityId));
		if (!transform) return false;
		*transform = value;
		return true;
	}

	bool WorldTooling::SetLight(EntityToolingTarget target, const components::LightComponent& value)
	{
		if (!IsValid(target)) return false;
		auto* light = m_World.GetRegistry().try_get<components::LightComponent>(
			static_cast<entt::entity>(target.m_EntityId));
		if (!light) return false;
		auto normalized = value;
		normalized.m_Type = static_cast<LightType>(std::clamp(static_cast<int>(value.m_Type),
			static_cast<int>(LightType::Directional), static_cast<int>(LightType::Point)));
		normalized.m_Intensity = std::max(value.m_Intensity, 0.0f);
		normalized.m_Range = std::max(value.m_Range, 0.001f);
		normalized.m_SpotAngle = std::clamp(value.m_SpotAngle, 0.001f, 179.0f);
		if (normalized.m_Type != light->m_Type && normalized.m_Type != LightType::Directional)
			normalized.m_DirectionalShadowSettings.reset();
		*light = normalized;
		return true;
	}
}
