
#include "gtest/gtest.h"
#include "gtest/main.h"

TEST(test, nonsense) {
  EXPECT_EQ(1, 1);
}

int main(int argc, char** argv) { return minimalistMain(argc, argv); }
