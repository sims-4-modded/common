/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <span>
#include <string_view>

#include <common/Result.h>
#include <mod_core/Signature.h>

#include "mod_game/Module.h"

namespace ts4_mod
{

/// The highest entropy of a code section, in bits for each byte, that the extender accepts as decrypted code. Machine
/// code has approximately 6 bits for each byte. The code of `TS4_x64.exe` on disk is encrypted and has approximately 8.
inline constexpr double k_DecryptedCodeEntropyLimit = 7.2;

/// The Shannon entropy of `bytes`, in bits for each byte: from 0 (one value only) to 8 (all values the same number of
/// times). Return 0 for no bytes.
[[nodiscard]] double ByteEntropy(std::span<const std::byte> bytes);

/// Make sure that the code section `section` of `module` is decrypted: its entropy is not more than `limit`. The
/// activation wrapper of EA decrypts the code of `TS4_x64.exe` at the start of the game. Thus the extender calls this
/// function before it scans the exe, and does not scan an exe that is still encrypted.
[[nodiscard]] ts4::Result<void> CheckDecrypted(const ModuleImage& module, std::string_view section = ".text",
                                               double limit = k_DecryptedCodeEntropyLimit);

/// Find the one place in the executable sections of `module` where the signature matches. Return an error that names
/// the signature (`name`) and the module, and tells the count, if the signature matches no place or more than one
/// place. A match cannot go across two sections. The function makes sure that the process can read each section
/// before it scans the section.
[[nodiscard]] ts4::Result<const std::byte*>
FindUniqueSignature(const ModuleImage& module, std::span<const SignatureByte> signature, std::string_view name);

/// Read the signature text (see `Signature::Parse`), then find its one match in `module`.
[[nodiscard]] ts4::Result<const std::byte*> FindUniqueSignature(const ModuleImage& module, std::string_view signatureText,
                                                                std::string_view name);

} // namespace ts4_mod
