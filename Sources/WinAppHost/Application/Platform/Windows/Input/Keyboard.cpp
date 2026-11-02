#include "Application/Platform/Windows/Input/Keyboard.h"

#include <cstring>

namespace gglab
{
	Keyboard::Keyboard(IGameInput* gameInput) noexcept :
		InputBase(gameInput, GameInputKind::GameInputKindKeyboard)
	{
	}

	void Keyboard::Update() noexcept
	{
		// An empty device set still publishes releases for the last connected keyboard.
		m_StateTracker.Update(GetState());
	}

	bool Keyboard::IsKeyPressed(KeyCode key) const noexcept
	{
		return m_StateTracker.m_Pressed.IsKeyDown(key);
	}

	bool Keyboard::IsKeyReleased(KeyCode key) const noexcept
	{
		return m_StateTracker.m_Released.IsKeyDown(key);
	}

	bool Keyboard::IsKeyHeld(KeyCode key) const noexcept
	{
		return m_StateTracker.m_LastState.IsKeyDown(key);
	}

	void Keyboard::Reset() noexcept
	{
		m_Devices.clear();
		m_StateTracker.Reset();
	}

	Keyboard::State Keyboard::GetState() noexcept
	{
		State state = {};
		if (!m_GameInput)
		{
			return state;
		}

		CopyConnectedDevices(m_Devices);
		for (const ComPtr<IGameInputDevice>& device : m_Devices)
		{
			ComPtr<IGameInputReading> reading;
			const HRESULT result = m_GameInput->GetCurrentReading(
				GameInputKindKeyboard, device.Get(), &reading);
			if (FAILED(result))
			{
				continue;
			}

			const uint32_t keyCount = reading->GetKeyCount();
			if (m_KeyStates.size() < keyCount)
			{
				m_KeyStates.resize(keyCount);
			}
			const uint32_t readCount = keyCount != 0 ?
				reading->GetKeyState(keyCount, m_KeyStates.data()) :
				0;
			// Each reading belongs to one device. An empty receiver keyboard report
			// must not clear keys held on another keyboard; union all successful readings.
			for (uint32_t index = 0; index < readCount; ++index)
			{
				auto vk = static_cast<int32_t>(m_KeyStates[index].virtualKey);
				state.SetKeyBit(vk);
			}
		}

		return state;
	}

	// StateTracker
	void Keyboard::StateTracker::Update(const State& state) noexcept
	{
		auto currPtr = reinterpret_cast<const uint32_t*>(&state);
		auto prevPtr = reinterpret_cast<const uint32_t*>(&m_LastState);
		auto releasedPtr = reinterpret_cast<uint32_t*>(&m_Released);
		auto pressedPtr = reinterpret_cast<uint32_t*>(&m_Pressed);

		for (size_t index = 0; index < (256 / 32); ++index)
		{
			*pressedPtr = *currPtr & ~(*prevPtr);
			*releasedPtr = ~(*currPtr) & *prevPtr;

			++currPtr;
			++prevPtr;
			++releasedPtr;
			++pressedPtr;
		}

		m_LastState = state;
	}

	void Keyboard::StateTracker::Reset() noexcept
	{
		std::memset(this, 0, sizeof(StateTracker));
	}
}
