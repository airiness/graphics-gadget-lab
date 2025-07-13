#pragma once
#include "InputBase.h"

namespace graphicsGadgetLab
{
	class Mouse final : public InputBase
	{
	public:
		Mouse() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Mouse);
		virtual ~Mouse() noexcept = default;

		virtual void Update() noexcept override;

	private:
		void GetState() noexcept;

	private:

	};
}