// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace VideoCommon::Fsr4 {

// Provider IDs are opaque. Select a version returned by AMD instead of synthesizing an ID.
constexpr bool IsVersion412(std::string_view name) {
    constexpr std::string_view version = "4.1.2";
    const auto is_version_char = [](char c) { return (c >= '0' && c <= '9') || c == '.'; };
    for (auto pos = name.find(version); pos != std::string_view::npos;
         pos = name.find(version, pos + version.size())) {
        const auto end = pos + version.size();
        if ((pos == 0 || !is_version_char(name[pos - 1])) &&
            (end == name.size() || !is_version_char(name[end]))) {
            return true;
        }
    }
    return false;
}

constexpr bool Requested(bool enabled, std::string_view override_value) {
    return override_value == "fsr4" || (enabled && override_value.empty());
}

} // namespace VideoCommon::Fsr4
