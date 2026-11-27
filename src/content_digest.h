#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace klang {

std::string Sha256(std::string_view contents);
std::optional<std::string> Sha256File(const std::filesystem::path &path);

}  // namespace klang
