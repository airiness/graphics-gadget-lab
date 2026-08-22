#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstdint>

namespace gglab
{
	enum class AppRuntimeLifecycleState : uint8_t
	{
		Uninitialized,
		Initializing,
		Running,
		Suspended,
		ExitRequested,
		Failed,
		ShuttingDown,
		Stopped,
	};

	enum class AppRuntimeInitializeResult : uint8_t
	{
		Succeeded,
		AlreadyInitialized,
		InvalidState,
		MissingBootstrapService,
		BootstrapServiceFailed,
	};

	enum class AppRuntimeTickResult : uint8_t
	{
		Continue,
		Suspended,
		Exit,
	};

	enum class AppHostEventType : uint8_t
	{
		Suspended,
		Resumed,
		ExitRequested,
	};

	// Minimal initialization seam for the first reusable lifecycle target.
	// Once Initialize is attempted, Shutdown is called exactly once regardless
	// of whether initialization succeeds. Broader host policy stays outside this contract.
	class AppRuntimeBootstrapServiceBase
	{
	public:
		virtual ~AppRuntimeBootstrapServiceBase() = default;

		[[nodiscard]] virtual bool Initialize() noexcept = 0;
		virtual void Shutdown() noexcept = 0;
	};

	struct GGLabAppRuntimeCreateInfo
	{
		AppRuntimeBootstrapServiceBase* m_BootstrapService = nullptr;
	};

	class GGLabAppRuntime final
	{
	public:
		GGLabAppRuntime() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(GGLabAppRuntime);
		~GGLabAppRuntime() noexcept;

		[[nodiscard]] AppRuntimeInitializeResult Initialize(
			const GGLabAppRuntimeCreateInfo& createInfo) noexcept;
		[[nodiscard]] AppRuntimeTickResult Tick() const noexcept;
		void HandleHostEvent(AppHostEventType eventType) noexcept;
		void Shutdown() noexcept;

		[[nodiscard]] AppRuntimeLifecycleState GetLifecycleState() const noexcept
		{
			return m_LifecycleState;
		}

	private:
		AppRuntimeBootstrapServiceBase* m_BootstrapService = nullptr;
		AppRuntimeLifecycleState m_LifecycleState = AppRuntimeLifecycleState::Uninitialized;
		bool m_BootstrapInitializationAttempted = false;
		bool m_ShutdownComplete = false;
	};
}
