// SPDX-License-Identifier: GPL-3.0-or-later

#include <string_view>
#include <catch2/catch_test_macros.hpp>
#include "video_core/fire_emblem_upscale.h"
#include "video_core/fsr4_policy.h"

TEST_CASE("FSR 4 only accepts the requested provider version", "[fsr4]") {
    using VideoCommon::Fsr4::IsVersion412;
    for (const auto name : {"4.1.2", "FSR 4.1.2", "AMD FSR 4.1.2 (ML)", "v4.1.2"}) {
        REQUIRE(IsVersion412(name));
    }
    for (const auto name : {"", "FSR 3.1.4", "4.1.1", "4.1.20", "14.1.2", "4.1.2.1"}) {
        REQUIRE_FALSE(IsVersion412(name));
    }
}

TEST_CASE("Explicit upscaler overrides take priority over the FSR 4 setting", "[fsr4]") {
    using VideoCommon::Fsr4::Requested;
    REQUIRE_FALSE(Requested(false, ""));
    REQUIRE(Requested(true, ""));
    for (const bool enabled : {false, true}) {
        REQUIRE(Requested(enabled, "fsr4"));
        for (const auto value : {"fsr", "dlss", "native", "standin", "invalid"}) {
            REQUIRE_FALSE(Requested(enabled, value));
        }
    }
}

TEST_CASE("FSR 4 retains the per-game DLSS input contract", "[fsr4]") {
    namespace FE = VideoCommon::FireEmblemUpscale;
    REQUIRE(FE::UseReplacement(FE::ProgramId, FE::OutputProgramKey, "fsr4"));
    REQUIRE_FALSE(FE::UseReplacement(FE::ProgramId + 1, FE::OutputProgramKey, "fsr4"));
    REQUIRE_FALSE(FE::UseReplacement(FE::ProgramId, FE::OutputProgramKey + 1, "fsr4"));
    REQUIRE_FALSE(FE::UseReplacement(FE::ProgramId, FE::OutputProgramKey, "native"));
}
