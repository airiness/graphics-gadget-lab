#pragma once

namespace gglab
{
	struct RenderView
	{
		Matrix m_View = Matrix::Identity;
		Matrix m_Proj = Matrix::Identity;
		Matrix m_ViewProj = Matrix::Identity;
		Matrix m_InvView = Matrix::Identity;
		Matrix m_InvProj = Matrix::Identity;
	};
}