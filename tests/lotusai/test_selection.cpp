#include "lotusai_utils_host.h"

#include <cstdio>
#include <string>
#include <vector>

#define TEST(name) static void name(); \
    struct name##_reg { name##_reg() { tests().push_back({#name, name}); } }; \
    static name##_reg name##_instance; \
    static void name()

struct TestEntry {
    const char* name;
    void (*fn)();
};

static std::vector<TestEntry>& tests() {
    static std::vector<TestEntry> entries;
    return entries;
}

static int g_failures = 0;

#define EXPECT_TRUE(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failures++; return; } } while (0)

#define EXPECT_EQ_INT(a, b) do { if ((a) != (b)) { \
    std::fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, \
        (b), (a)); g_failures++; return; } } while (0)

#define EXPECT_EQ_STR(a, b) do { \
    const std::string _a(a); const std::string _b(b); \
    if (_a != _b) { \
    std::fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, \
        _b.c_str(), _a.c_str()); g_failures++; return; } } while (0)

TEST(option_from_point_three_rows) {
    const int h = 320;
    const int offset = LOTUSAI_CONTENT_Y_OFFSET;
    const int count = 3;
    const int row_h = (h - offset) / count;

    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(80, h, count), 0);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(80 + row_h - 1, h, count), 0);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(80 + row_h, h, count), 1);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(80 + 2 * row_h, h, count), 2);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(319, h, count), 2);
}

TEST(option_from_point_out_of_bounds) {
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(79, 320, 3), -1);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(320, 320, 3), -1);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(100, 320, 0), -1);
}

TEST(option_from_point_single_recipe) {
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(80, 320, 1), 0);
    EXPECT_EQ_INT(LotusAiOptionIndexFromPoint(319, 320, 1), 0);
}

TEST(select_index_no_recipes) {
    EXPECT_EQ_STR(LotusAiSelectIndexErrorMessage(0, 0),
                  "No recipes loaded yet. Please search for recipes first.");
}

TEST(select_index_invalid_high) {
    EXPECT_EQ_STR(LotusAiSelectIndexErrorMessage(1, 1),
                  "Invalid selection. Please choose a number between 1 and 1.");
}

TEST(select_index_invalid_negative) {
    EXPECT_EQ_STR(LotusAiSelectIndexErrorMessage(-1, 2),
                  "Invalid selection. Please choose a number between 1 and 2.");
}

TEST(select_index_valid) {
    EXPECT_TRUE(LotusAiSelectIndexErrorMessage(0, 3).empty());
    EXPECT_TRUE(LotusAiSelectIndexErrorMessage(2, 3).empty());
    EXPECT_TRUE(LotusAiIsValidRecipeIndex(0, 3));
    EXPECT_TRUE(!LotusAiIsValidRecipeIndex(3, 3));
}

int main() {
    for (const auto& t : tests()) {
        int before = g_failures;
        t.fn();
        if (g_failures == before)
            std::printf("PASS %s\n", t.name);
    }
    if (g_failures > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All selection tests passed.\n");
    return 0;
}
