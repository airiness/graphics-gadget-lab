#pragma once

namespace gglab
{
	class SceneManager
	{
	public:
		SceneManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(SceneManager);
		~SceneManager();


	private:
		std::vector<SceneBase> m_Scene;
	};
}