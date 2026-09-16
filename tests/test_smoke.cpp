#include "test.h"

// Proves the build + ctest wiring works before real code lands.
int main() {
    CHECK(1 + 1 == 2);
    CHECK_EQ(std::string("dsl"), std::string("dsl"));
    TEST_SUMMARY("smoke");
}
