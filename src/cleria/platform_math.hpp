// atan2f as the Windows build computes it (platform_math.cpp, issue #46).
#pragma once

namespace cleria {

// Bit for bit the x64 MSVC C library's atan2f (AMD's libm algorithm, which the Universal CRT ships): the
// arctangent in double from a table of atan(j/256) and a short series, rounded to float once.
float msvc_atan2f(float y, float x) noexcept;

}  // namespace cleria
