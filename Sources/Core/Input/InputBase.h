#pragma once
#include "Core/Platform/Win/ComTypes.h"

#include <Windows.h>
#include <GameInput.h>

#include <cstdint>

namespace gglab
{
	class InputBase
	{
	public:
		InputBase(IGameInput* gameInput, GameInputKind inputKind) noexcept;
		virtual ~InputBase() noexcept;

		virtual void Update() noexcept = 0;
		bool IsAvailable() const noexcept;
		bool IsConnected() const noexcept;

	protected:
		static void CALLBACK OnGameInputDevice(GameInputCallbackToken callbackToken, void* context,
			IGameInputDevice* gameInputDevice, uint64_t timestamp,
			GameInputDeviceStatus currentStatus, GameInputDeviceStatus previousStatus) noexcept;

	protected:
		ComPtr<IGameInput> m_GameInput;
		GameInputCallbackToken m_CallbackToken = 0;
		uint32_t m_Connected = 0;
	};
}
