#pragma once

#include "GGLabRuntime/Scene/WorldTooling.h"

namespace gglab
{
	class World;

	class WorldTooling final : public WorldToolingViewBase, public WorldToolingControlBase
	{
	public:
		explicit WorldTooling(World& world);
		[[nodiscard]] WorldToolingSnapshot GetEntities() const override;
		[[nodiscard]] EntityToolingTarget CreateEntity() override;
		bool DestroyEntity(EntityToolingTarget target) override;
		bool AddTransform(EntityToolingTarget target) override;
		bool AddLight(EntityToolingTarget target) override;
		bool AddModel(EntityToolingTarget target, ModelID model) override;
		bool SetTransform(EntityToolingTarget target, const components::TransformComponent& value) override;
		bool SetLight(EntityToolingTarget target, const components::LightComponent& value) override;

	private:
		[[nodiscard]] bool HasWorld() const noexcept;
		[[nodiscard]] bool IsValid(EntityToolingTarget target) const noexcept;
		World& m_World;
		uint64_t m_WorldId = 0;
	};
}
