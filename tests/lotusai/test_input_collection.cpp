#include "test_helpers.h"
#include "lotusai_utils_host.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static int g_failures = 0;

struct TestEntry {
    const char* name;
    void (*fn)();
};

static std::vector<TestEntry>& tests() {
    static std::vector<TestEntry> entries;
    return entries;
}

#define TEST(name) static void name(); \
    struct name##_reg { name##_reg() { tests().push_back({#name, name}); } }; \
    static name##_reg name##_instance; \
    static void name()

#define EXPECT_TRUE(cond) do { if (!(cond)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failures++; return; } } while (0)

#define EXPECT_EQ_STR(a, b) do { \
    const std::string _a(a); const std::string _b(b); \
    if (_a != _b) { \
    std::fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, \
        _b.c_str(), _a.c_str()); g_failures++; return; } } while (0)

#define EXPECT_CONTAINS(hay, needle) do { \
    if ((hay).find(needle) == std::string::npos) { \
    std::fprintf(stderr, "FAIL %s:%d: '%s' not in '%s'\n", __FILE__, __LINE__, \
        needle, (hay).c_str()); g_failures++; return; } } while (0)

TEST(split_csv_trims_and_skips_empty) {
    auto items = LotusAiSplitCsv(" ,, a , b , ");
    EXPECT_TRUE(items.size() == 2);
    EXPECT_EQ_STR(items[0], "a");
    EXPECT_EQ_STR(items[1], "b");
}

TEST(required_only_body) {
    auto props = MakeRecommendProps("chicken, rice");
    auto result = LotusAiBuildRecommendRequestBody(props, 3);
    EXPECT_TRUE(result.error.empty());
    EXPECT_CONTAINS(result.body, "\"ingredients\":[\"chicken\",\"rice\"]");
    EXPECT_CONTAINS(result.body, "\"top_k\":3");
    EXPECT_TRUE(!JsonHasKey(result.body, "allergens"));
}

TEST(empty_ingredients_returns_error) {
    auto props = MakeRecommendProps("");
    auto result = LotusAiBuildRecommendRequestBody(props, 3);
    EXPECT_TRUE(!result.error.empty());
    EXPECT_TRUE(result.body.empty());
}

TEST(core_optionals_present) {
    auto props = MakeRecommendProps(
        "chicken", "type 2 diabetes", "lunch", "adult", "Asian");
    auto result = LotusAiBuildRecommendRequestBody(props, 3);
    EXPECT_CONTAINS(result.body, "\"conditions\":[\"type 2 diabetes\"]");
    EXPECT_CONTAINS(result.body, "\"meal\":\"lunch\"");
    EXPECT_CONTAINS(result.body, "\"age\":\"adult\"");
    EXPECT_CONTAINS(result.body, "\"cuisine\":\"Asian\"");
}

TEST(preference_fields_present) {
    auto props = MakeRecommendProps(
        "chicken", "", "", "", "", 3,
        "stove,microwave", "peanut,milk", "", true);
    auto result = LotusAiBuildRecommendRequestBody(props, 3);
    EXPECT_EQ_STR(JsonGetStringArrayJoined(result.body, "cooking_tools"), "stove|microwave");
    EXPECT_EQ_STR(JsonGetStringArrayJoined(result.body, "allergens"), "peanut|milk");
    EXPECT_CONTAINS(result.body, "\"plant_based\":true");
}

TEST(excluded_ingredients_present) {
    auto props = MakeRecommendProps(
        "chicken", "", "", "", "", 3,
        "", "", "cilantro, mushrooms");
    auto result = LotusAiBuildRecommendRequestBody(props, 3);
    EXPECT_EQ_STR(JsonGetStringArrayJoined(result.body, "excluded_ingredients"),
                  "cilantro|mushrooms");
}

TEST(allergens_and_excluded_both_present) {
    auto props = MakeRecommendProps(
        "chicken", "", "", "", "", 3,
        "", "peanut", "mushrooms");
    auto result = LotusAiBuildRecommendRequestBody(props, 3);
    EXPECT_TRUE(JsonHasKey(result.body, "allergens"));
    EXPECT_TRUE(JsonHasKey(result.body, "excluded_ingredients"));
}

TEST(top_k_out_of_range_throws) {
    bool threw = false;
    try {
        Property p("top_k", kPropertyTypeInteger, 15, 3, 12);
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
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
    std::printf("All input collection tests passed.\n");
    return 0;
}
