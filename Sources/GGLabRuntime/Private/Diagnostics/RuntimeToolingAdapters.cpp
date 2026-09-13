#include "GGLabRuntime/Diagnostics/RuntimeToolingAdapters.h"

#include "Diagnostics/AssetToolingControl.h"
#include "Diagnostics/CameraTooling.h"
#include "Diagnostics/DirectionalLightTooling.h"
#include "Diagnostics/WorldTooling.h"

#include <memory>

namespace gglab
{
	struct RuntimeToolingAdapters::Impl final
	{
		Impl(World& world, AssetManager& assets, CameraRig& cameras) :
			m_DirectionalLight(world),
			m_Assets(assets),
			m_Cameras(cameras),
			m_World(world)
		{
		}

		DirectionalLightTooling m_DirectionalLight;
		AssetToolingControl m_Assets;
		CameraTooling m_Cameras;
		WorldTooling m_World;
	};

	RuntimeToolingAdapters::RuntimeToolingAdapters(
		World& world, AssetManager& assets, CameraRig& cameras) :
		m_Impl(std::make_unique<Impl>(world, assets, cameras))
	{
	}

	RuntimeToolingAdapters::~RuntimeToolingAdapters() = default;

	const CameraToolingViewBase& RuntimeToolingAdapters::GetCameraView() const noexcept
	{
		return m_Impl->m_Cameras;
	}

	CameraToolingControlBase& RuntimeToolingAdapters::GetCameraControl() noexcept
	{
		return m_Impl->m_Cameras;
	}

	const WorldToolingViewBase& RuntimeToolingAdapters::GetWorldView() const noexcept
	{
		return m_Impl->m_World;
	}

	WorldToolingControlBase& RuntimeToolingAdapters::GetWorldControl() noexcept
	{
		return m_Impl->m_World;
	}

	const DirectionalLightViewBase& RuntimeToolingAdapters::GetDirectionalLightView() const noexcept
	{
		return m_Impl->m_DirectionalLight;
	}

	DirectionalLightControlBase& RuntimeToolingAdapters::GetDirectionalLightControl() noexcept
	{
		return m_Impl->m_DirectionalLight;
	}

	AssetToolingControlBase& RuntimeToolingAdapters::GetAssetControl() noexcept
	{
		return m_Impl->m_Assets;
	}
}
