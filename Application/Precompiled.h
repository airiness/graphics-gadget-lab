#pragma once

// Windows
#ifndef NOMINMIX
#define NOMINMAX
#endif // !NOMINMIX

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <Shlwapi.h>	// PathRemoveFileSpecW

// C++
#include <cstdint>
#include <string>
#include <memory>