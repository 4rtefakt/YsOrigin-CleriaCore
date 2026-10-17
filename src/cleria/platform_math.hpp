// atan2f as the Windows build computes it (platform_math.cpp, issue #46).
#pragma once

namespace cleria {

// Bit for bit the x64 MSVC C library's atan2f (AMD's libm algorithm, which the Universal CRT ships): the
// arctangent in double from a table of atan(j/256) and a short series, rounded to float once.
float msvc_atan2f(float y, float x) noexcept;

// The rest of the C library functions the engine calls, bit for bit as the x64 Universal CRT computes them on an
// FMA3 CPU (msvc_*.cpp): its FMA3 branch, ported from ucrtbase.dll 10.0.26100.9711.
float msvc_sinf(float x);
float msvc_cosf(float x);
float msvc_tanf(float x);
double msvc_sin(double x);
double msvc_cos(double x);
double msvc_tan(double x);
float msvc_asinf(float x);
float msvc_acosf(float x);
double msvc_asin(double x);
double msvc_acos(double x);
double msvc_atan2(double y, double x);
double msvc_pow(double x, double y);
float msvc_powf(float x, float y);
float msvc_hypotf(float x, float y);     // the CRT's _hypotf (std::hypot(float, float) on Windows)
double msvc_exp(double x);
double msvc_log(double x);
double msvc_log10(double x);

}  // namespace cleria
