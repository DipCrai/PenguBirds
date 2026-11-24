// Best-effort extraction of the main icon from a Windows PE file.
#pragma once

#include <filesystem>
#include <string>

namespace pengubirds {

// Writes the largest RT_GROUP_ICON image from `pe_file` as a one-image
// .ico to `out`. Returns false if the PE has no usable icon.
bool ExtractIcon(const std::filesystem::path& pe_file,
                 const std::filesystem::path& out, std::string* error);

}  // namespace pengubirds
