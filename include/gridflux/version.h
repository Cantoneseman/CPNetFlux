#pragma once

#include <string_view>

namespace gridflux {

constexpr std::string_view kProjectName = "GridFlux";
constexpr std::string_view kProjectVersion = "0.1.0";
constexpr int kRequiredCxxStandard = 202002;

std::string_view projectName() noexcept;
std::string_view projectVersion() noexcept;

}  // namespace gridflux
