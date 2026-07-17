// The layout lives entirely in the header; this TU exists so its static_asserts are
// compiled on every build — a 0xAC record we reinterpret from the game's .data is
// exactly the kind of thing that must fail loudly if a field ever drifts.
#include "cleria/csprite.hpp"
