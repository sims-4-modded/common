/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <string>
#include <string_view>

namespace ts4_mod
{

/// Change UTF-8 text into the UTF-16 text of the Windows functions. A byte that is not correct UTF-8 becomes U+FFFD.
[[nodiscard]] std::wstring ToWide(std::string_view text);

/// Change UTF-16 text of the Windows functions into UTF-8 text.
[[nodiscard]] std::string ToUtf8(std::wstring_view text);

} // namespace ts4_mod
