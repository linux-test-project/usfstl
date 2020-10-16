/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <usfstl/test.h>
#include <lib.h>

#if defined(CONFIG_A)
#define OFFSET 42
#elif defined(CONFIG_B)
#define OFFSET 43
#else
#error "unknown configuration"
#endif

// ----------- simple test
void test_simple(const struct usfstl_test *test, void *testcase)
{
	USFSTL_ASSERT_EQ(dummy1(), OFFSET, "%d");
	USFSTL_ASSERT_EQ(dummy2(100), 100 + OFFSET, "%d");
}
USFSTL_UNIT_TEST(test_simple, NULL, NO_CASES);


// ----------- test with multiple test cases
struct testcase {
	struct usfstl_testcase generic;
	int input, output;
};

void test_cases(const struct usfstl_test *test, void *testcase)
{
	struct testcase *tc = testcase;

	USFSTL_ASSERT_EQ(dummy2(tc->input), tc->output, "%d");

	// also check that the globals are restored between
	// each test run, i.e. the g_sum is just one input
	USFSTL_ASSERT_EQ(g_sum, tc->input, "%d");
}

static struct testcase cases[] = {
	{ .input = 42, .output = 42 + OFFSET },
	{ .input = 100, .output = 100 + OFFSET },
	{ .input = 100000, .output = 100000 + OFFSET },
};
USFSTL_UNIT_TEST(test_cases, NULL, cases);


// ----------- test with multiple test cases from generator
static struct testcase g_generator_testcase;

void *generator(const struct usfstl_test *test, unsigned int i)
{
	if (i < 100) {
		g_generator_testcase.input = 100 * i;
		g_generator_testcase.output = 100 * i + OFFSET;
		return &g_generator_testcase;
	}

	return NULL;
}
USFSTL_UNIT_TEST_NAMED(test_cases_gen, test_cases, NULL, NO_CASES,
		       .case_generator=generator);


// ----------- test with function override

int dummy1_stub(void)
{
	return 55;
}

void test_override(const struct usfstl_test *test, void *testcase)
{
	USFSTL_STUB(dummy1, dummy1_stub);

	USFSTL_ASSERT_EQ(dummy2(100), 155, "%d");
}
USFSTL_UNIT_TEST(test_override, NULL, NO_CASES);


// ----------- test with function override
void test_no_print(const struct usfstl_test *test, void *testcase)
{
	dummy3(100);
	USFSTL_ASSERT_EQ(g_printed_value, 100 + OFFSET, "%d");
}
USFSTL_UNIT_TEST(test_no_print, NULL, NO_CASES);

// ----------- special code cases macros
#define TEST_NAME test_code_cases

#if 1
USFSTL_CODE_TEST_CASE()
{
	dummy3(3);
	USFSTL_ASSERT_EQ(g_printed_value, 3 + OFFSET, "%d");
}
#endif

#if 1
USFSTL_CODE_TEST_CASES(
	TC_ARG(int, n),
	TC_DATA(.n = 7)
	TC_DATA(.n = 8)
)
{
	dummy3(data->n);
	USFSTL_ASSERT_EQ(g_printed_value, data->n + OFFSET, "%d");
}
#endif

USFSTL_CODE_TEST_FUNC()
{
	USFSTL_ASSERT_EQ(g_printed_value, 0, "%d");
	USFSTL_RUN_CODE_TEST_CASE();
}

USFSTL_CODE_TEST(USFSTL_UNIT_TEST, NULL);
