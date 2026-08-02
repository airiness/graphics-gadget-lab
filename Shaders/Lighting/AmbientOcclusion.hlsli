#pragma once

float ResolveDiffuseIBLVisibility(float materialAO, float gtao)
{
	return saturate(materialAO * gtao);
}

float ResolveSpecularIBLVisibility(float materialAO)
{
	return saturate(materialAO);
}
