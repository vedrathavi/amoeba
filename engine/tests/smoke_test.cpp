#include "amoeba/engine.hpp"

#include <gtest/gtest.h>

using namespace std;

TEST(FoundationSmokeTest, BasicSanity) {
    EXPECT_TRUE(true);
}

TEST(EngineSmokeTest, EngineMetadata) {
    EXPECT_EQ(amoeba::get_name(), "Amoeba");
    EXPECT_EQ(amoeba::get_version(), "0.1.0");
    EXPECT_EQ(amoeba::get_description(), "Source Code Search & Indexing Engine");
}
