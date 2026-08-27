#pragma once

// Normalize compiler-provided platform facts into the vocabulary consumed by
// first-party GGLab code. Platform macros are always defined to 0 or 1.
#if defined(_WIN32)
#define GGLAB_PLATFORM_WINDOWS 1
#else
#define GGLAB_PLATFORM_WINDOWS 0
#endif
