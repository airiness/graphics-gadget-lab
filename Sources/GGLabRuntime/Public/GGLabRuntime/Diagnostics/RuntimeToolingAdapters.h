#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"

#include <memory>

namespace gglab
{
	class AssetManager;
	class AssetToolingControlBase;
	class CameraRig;
	class CameraToolingControlBase;
	class CameraToolingViewBase;
	class DirectionalLightControlBase;
	class DirectionalLightViewBase;
	class World;
	class WorldToolingControlBase;
	class WorldToolingViewBase;

	// Owns the concrete Runtime adapters borrowed by one synchronous tooling draw.
	class RuntimeToolingAdapters final
	{
	public:
		RuntimeToolingAdapters(World& world, AssetManager& assets, CameraRig& cameras);
		GGLAB_DELETE_COPYABLE_MOVABLE(RuntimeToolingAdapters);
		~RuntimeToolingAdapters();

		[[nodiscard]] const CameraToolingViewBase& GetCameraView() const noexcept;
		[[nodiscard]] CameraToolingControlBase& GetCameraControl() noexcept;
		[[nodiscard]] const WorldToolingViewBase& GetWorldView() const noexcept;
		[[nodiscard]] WorldToolingControlBase& GetWorldControl() noexcept;
		[[nodiscard]] const DirectionalLightViewBase& GetDirectionalLightView() const noexcept;
		[[nodiscard]] DirectionalLightControlBase& GetDirectionalLightControl() noexcept;
		[[nodiscard]] AssetToolingControlBase& GetAssetControl() noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
