// Window text escapes: FUN_0057ad90, which go_wnddialog runs on every Window_Msg /
// Window_Treasure / menu string before layout (callers 0x5781f0, 0x5785d0, 0x57b030).
//
// Script strings carry two-byte escapes, e.g. S_BOX01.XSO's "Acquired\n<color:...>Celcetan
// Panacea<color:> x 1." holds a literal backslash + 'n'. Drawn raw, the backslash shows as the
// yen sign of the Japanese code page. The engine rewrites them in place:
//   \r -> CR    \n -> LF (plus a space in language 0 when the caller asks; not for English)
//   \t -> space
//   \a -> "Adol"   \c -> "Christin"          (the name buffers 0x6d6020 / 0x6d602c)
//   \x1 / \f1 -> "Adol"   \x2 -> "Christin"   \x3 -> "Adol Christin"   \x4 -> 0x6d6040 (" ")
//   \x0 -> the first character of "Adol", then the string ends (the engine writes a NUL)
//   any other \c -> c (the backslash is dropped)
// then trims trailing CR / LF / spaces. Japanese lead bytes are copied as pairs only in
// languages 0/1 with a Japanese code page; CleriaCore's English text has none.
#pragma once
#include <string>
#include <string_view>

namespace cleria {

std::string wnd_unescape(std::string_view s);

}  // namespace cleria
