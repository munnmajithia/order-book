// Smoke test: every module anchor links and answers. Anchors are retired as
// modules gain real code; their lines here go with them.
#include "engine/module.hpp"
#include "queue/module.hpp"

#include <gtest/gtest.h>
#include <string_view>

TEST(Smoke, ModulesLink) {
    EXPECT_EQ(std::string_view{ob::engine::module_name()}, "engine");
    EXPECT_EQ(std::string_view{ob::queue::module_name()}, "queue");
}
