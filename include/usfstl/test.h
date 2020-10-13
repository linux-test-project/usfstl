/*
 * Copyright (C) 2018 - 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_TEST_H_
#define _USFSTL_TEST_H_
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include "macros.h"
#include "assert.h"
#include "opt.h"

/*
 * usfstl_get_function_info - get information about function
 *
 * Fill the pointers passed with the corresponding information
 * about the function.
 */
void usfstl_get_function_info(const void *ptr, char *funcname,
			      char *filename, unsigned int *lineno);

/*
 * Test project name, really just derived from the binary that's running.
 * Could be overwritten by an initializer, but not later than that.
 */
extern const char * const g_usfstl_projectname;
// temporarily:
#define g_usfstl_testname g_usfstl_projectname

/*
 * current test and, if test has multiple cases, test case data
 */
extern struct usfstl_test *g_usfstl_current_test;
extern void *g_usfstl_current_test_case_data;
extern int g_usfstl_current_test_num;
extern int g_usfstl_current_case_num;
/*
 * NOTE: this may be NULL despite running a testcase, if there are
 *	 no cases, or if a generator function is used but doesn't
 *	 set case_generator_has_generic to true.
 */
extern struct usfstl_testcase *g_usfstl_current_testcase;


void usfstl_void_stub(void);
void usfstl_install_stub(const char *fname, const void *repl,
			 const char *replname, const char *file,
			 unsigned int line, bool check_args);

#define USFSTL_STUB(_fn, _repl)	usfstl_install_stub(#_fn, _repl, #_repl, __FILE__, __LINE__, true)
#define USFSTL_STUB_VOID(_fn)	usfstl_install_stub(#_fn, usfstl_void_stub,	NULL, __FILE__, __LINE__, false)
#define USFSTL_DONT_STUB(_fn)	usfstl_install_stub(#_fn, NULL, NULL, __FILE__, __LINE__, false)

/*
 * Stub a function to just return a constant; arguments are ignored.
 * Note that this only works if your test runs within the scope of
 * the macro, i.e. you cannot do the stubbing in a separate function,
 * return, and then run the test.
 */
#define USFSTL_STUB_TYPE(_fn, _type, _value)	do {			\
	type __repl_fn_##__LINE__(void) { return _value; }		\
	usfstl_install_stub(#_fn, _repl, __repl_fn_##__LINE__,		\
			    __FILE__, __LINE__, false);			\
} while (0)
#define USFSTL_STUB_INT(fn, val)	USFSTL_STUB_TYPE(fn, int, val)
#define USFSTL_STUB_UINT(fn, val)	USFSTL_STUB_TYPE(fn, unsigned int, val)
#define USFSTL_STUB_PTR(fn, ptr)	USFSTL_STUB_TYPE(fn, void *, val)

/*
 * Normally, before each test iteration all globals are reset to their
 * initial state, as the program started up. Those that are declared
 * with this macro, like e.g.
 *	static int USFSTL_NORESTORE_VAR(my_value);
 * however, aren't saved/restored. Use sparingly, but can be useful in
 * parts of the support code.
 */
#define USFSTL_NORESTORE_VAR(var)	var __attribute__((section("usfstl_norestore")))

enum usfstl_testcase_status {
	USFSTL_STATUS_SUCCESS = 0,
	USFSTL_STATUS_ASSERTION_FAILED,
	USFSTL_STATUS_NEGATIVE_TEST_NO_ERROR,
	USFSTL_STATUS_WATCHDOG_TIMEOUT,
	/* internal, never reported: */
	USFSTL_STATUS_REMOTE_SUCCESS,
	USFSTL_STATUS_NEGATIVE_TEST_SUCCEEDED,
	USFSTL_STATUS_OUT_OF_CASES,
	USFSTL_STATUS_SKIPPED,
};

struct usfstl_test {
	void (*fn)(struct usfstl_test *test, void *testcase);
	const void *extra_data;
	const char *name;
	const void *testcases;
	void *(*case_generator)(struct usfstl_test *test, unsigned int idx);
	size_t testcase_size;
	size_t testcase_count;
	unsigned int testcase_generic_offset;
	bool failing;
	bool flow_test;
	bool case_generator_has_generic;

	const void *negative_data;
	unsigned int max_cpu_time_ms;
	void (*pre)(struct usfstl_test *test, void *testcase,
		    int test_num, int case_num);
	void (*post)(struct usfstl_test *test, void *testcase,
		     int test_num, int case_num,
		     enum usfstl_testcase_status status);

	const char * const *requirements;

	const char * const *tested_files;
};

/*
 * USFSTL embedding support: you can embed USFSTL into a separate
 * test runner program/frontend and then control the test
 * execution from there, rather than having it controlled by
 * usfstl itself.
 *
 * In this case, you should define USFSTL_LIBRARY when compiling
 * usfstl, and then need to call usfstl_init() once, and
 * usfstl_run_test() or usfstl_run_named_test() for each test.
 *
 * Note that in the library case,
 *
 * - USFSTL_TEST() will create a struct usfstl_test usfstl_test_<func-name>
 * - USFSTL_TEST_NAMED() will create a struct usfstl_test usfstl_test_<name>
 *
 * You need these structs to pass to usfstl_run_test().
 */
#ifdef USFSTL_LIBRARY
/*
 * Initialize usfstl, using the command line arguments passed.
 * Note that argv[0] must be the program name for usfstl to find
 * some files it needs and to parse itself (dwarf info)
 */
int usfstl_init(int argc, char **argv);

enum usfstl_testcase_status usfstl_run_test(struct usfstl_test *tc);
#endif /* USFSTL_LIBRARY */

/*
 * Embed this struct into every test case passed to the tests below
 */
struct usfstl_testcase {
	const char *name;
	const char *requirement;
	const void *negative_data;
	bool failing;
};

#define _TOGETHER(...) __VA_ARGS__

#define __USFSTL_EMPTY_NO_CASES _,
#define _USFSTL_ASSIGN_TESTCASES1(cases)				\
	.testcases = cases,						\
	.testcase_size = sizeof(cases[0]),				\
	.testcase_count = sizeof(cases)/sizeof(cases[0]),		\
	.testcase_generic_offset = (int)((char *)&((typeof(&cases[0]))0)->generic - \
					 (char *)0),

#define _USFSTL_ASSIGN_TESTCASES2(...)

/* distinguish NO_CASES from normal cases */
#define _USFSTL_ASSIGN_TESTCASES(arg)	__USFSTL_IF_CASES(EMPTY_ ## arg)(arg)
#define ____USFSTL_IF_CASES(x)		_USFSTL_ASSIGN_TESTCASES ##x
#define ___USFSTL_IF_CASES(x)		____USFSTL_IF_CASES(x)
#define __USFSTL_IF_CASES(x)		___USFSTL_IF_CASES(USFSTL_COUNT(__USFSTL_##x))

#define USFSTL_TEST_SETUP(func, extra, cases, _flow, _negative, _max_cpu, ...)	\
	{									\
		.fn = func,							\
		.extra_data = extra,						\
		_USFSTL_ASSIGN_TESTCASES(cases)					\
		.flow_test = _flow,						\
		.negative_data = _negative,					\
		.max_cpu_time_ms = _max_cpu,					\
		__VA_ARGS__							\
	}

/*
 * Use this macro like so:
 * USFSTL_TEST(fn, extra, cases, flow, negative, max_cpu,
 *	       REQUIREMENTS("req1", "req2"),
 *             .negative_data = xyz,
 *	       ...);
 *
 * This will make the test track this requirement, and if all of its
 * test cases pass the requirement is considered to have been tested
 * and passed, if any test case fails it's considered to have been
 * tested and failed.
 */
#define REQUIREMENTS(...)		(_REQUIREMENTS(__VA_ARGS__))

#define ___SELECT_REQUIREMENTS_REQUIREMENTS(l, ...) \
	static const char * const _usfstl_test_req##l[] = { __VA_ARGS__, NULL };
#define __SELECT_REQUIREMENTS_REQUIREMENTS(l, ...) \
	___SELECT_REQUIREMENTS_REQUIREMENTS(l, __VA_ARGS__)
#define _SELECT_REQUIREMENTS_REQUIREMENTS(...) \
	__SELECT_REQUIREMENTS_REQUIREMENTS(__LINE__, __VA_ARGS__)
#define _SEL_REQUIREMENTS(...)			, _SELECT_REQUIREMENTS ## __VA_ARGS__

#define ___MAKE_REQLINK_REQUIREMENTS(l)	.requirements = _usfstl_test_req##l,
#define __MAKE_REQLINK_REQUIREMENTS(l)	___MAKE_REQLINK_REQUIREMENTS(l)
#define _MAKE_REQLINK_REQUIREMENTS(...)	__MAKE_REQLINK_REQUIREMENTS(__LINE__)
#define _MK_REQLINK(...)		, _MAKE_REQLINK ## __VA_ARGS__

/* Note: usfstl_test_##n is not static to enforce unique names at compile/link time */
#define USFSTL_TEST_NAMED(n, fn, extra, cases, _flow, _negative, _max_cpu, ...)	\
	USFSTL_MAP(_SELECT, _SEL_REQUIREMENTS, __VA_ARGS__)			\
	static const char * const usfstl_tested_files_##n[] =			\
		{ USFSTL_TESTED_FILES NULL };					\
	const struct usfstl_test usfstl_test_##n =				\
		USFSTL_TEST_SETUP(fn, extra, cases, _flow, _negative, _max_cpu,	\
				  .tested_files = usfstl_tested_files_##n,	\
				  .name=#n					\
				  USFSTL_MAP(_SELECT, _MK_REQLINK, __VA_ARGS__)	\
				  USFSTL_MAP(_SELECT_NOPARENS, _REMOVE,		\
					     __VA_ARGS__));			\
	static const struct usfstl_test * const usfstl_test_##n##_ptr		\
	__attribute__((used, section("usfstl_tests"))) = &usfstl_test_##n
#define USFSTL_TEST(fn, extra, cases, _flow, _negative, _max_cpu, ...)		\
	USFSTL_TEST_NAMED(fn, fn, extra, cases, _flow, _negative, _max_cpu, __VA_ARGS__)

#define USFSTL_DEFAULT_MAX_CPU_TIME_MS					30000

#define USFSTL_UNIT_TEST_TIMEOUT(f, e, cases, timeout, ...)		USFSTL_TEST(f, e, cases, false, NULL, timeout, ##__VA_ARGS__)
#define USFSTL_FLOW_TEST_TIMEOUT(f, e, cases, timeout, ...)		USFSTL_TEST(f, e, cases, true, NULL, timeout, ##__VA_ARGS__)
#define USFSTL_NEG_UNIT_TEST_TIMEOUT(f, e, cases, data, timeout, ...)	USFSTL_TEST(f, e, cases, false, data, timeout, ##__VA_ARGS__)
#define USFSTL_NEG_FLOW_TEST_TIMEOUT(f, e, cases, data, timeout, ...)	USFSTL_TEST(f, e, cases, true, data, timeout, ##__VA_ARGS__)
#define USFSTL_UNIT_TEST(f, e, cases, ...)				USFSTL_UNIT_TEST_TIMEOUT(f, e, cases, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)
#define USFSTL_FLOW_TEST(f, e, cases, ...)				USFSTL_FLOW_TEST_TIMEOUT(f, e, cases, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)
#define USFSTL_NEG_UNIT_TEST(f, e, cases, data, ...)			USFSTL_NEG_UNIT_TEST_TIMEOUT(f, e, cases, data, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)
#define USFSTL_NEG_FLOW_TEST(f, e, cases, data, ...)			USFSTL_NEG_FLOW_TEST_TIMEOUT(f, e, cases, data, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)

#define USFSTL_UNIT_TEST_TIMEOUT_NAMED(n, f, e, cases, timeout, ...)		USFSTL_TEST_NAMED(n, f, e, cases, false, NULL, timeout, ##__VA_ARGS__)
#define USFSTL_FLOW_TEST_TIMEOUT_NAMED(n, f, e, cases, timeout, ...)		USFSTL_TEST_NAMED(n, f, e, cases, true, NULL, timeout, ##__VA_ARGS__)
#define USFSTL_NEG_UNIT_TEST_TIMEOUT_NAMED(n, f, e, cases, data, timeout, ...)	USFSTL_TEST_NAMED(n, f, e, cases, false, data, timeout, ##__VA_ARGS__)
#define USFSTL_NEG_FLOW_TEST_TIMEOUT_NAMED(n, f, e, cases, data, timeout, ...)	USFSTL_TEST_NAMED(n, f, e, cases, true, data, timeout, ##__VA_ARGS__)
#define USFSTL_UNIT_TEST_NAMED(n, f, e, cases, ...)				USFSTL_UNIT_TEST_TIMEOUT_NAMED(n, f, e, cases, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)
#define USFSTL_FLOW_TEST_NAMED(n, f, e, cases, ...)				USFSTL_FLOW_TEST_TIMEOUT_NAMED(n, f, e, cases, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)
#define USFSTL_NEG_UNIT_TEST_NAMED(n, f, e, cases, data, ...)			USFSTL_NEG_UNIT_TEST_TIMEOUT_NAMED(n, f, e, cases, data, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)
#define USFSTL_NEG_FLOW_TEST_NAMED(n, f, e, cases, data, ...)			USFSTL_NEG_FLOW_TEST_TIMEOUT_NAMED(n, f, e, cases, data, USFSTL_DEFAULT_MAX_CPU_TIME_MS, ##__VA_ARGS__)

/*
 * Code-driven testcases
 *
 * This enables you to write some test cases for the same test case code,
 * in a nicer way. Preferably, do only one such test in a single C file.
 *
 * #define TEST_NAME my_test_name
 *
 * USFSTL_CODE_TEST_CASE()
 * {
 *	some_code();
 * }
 *
 * USFSTL_CODE_TEST_CASE()
 * {
 *	some_code();
 *	other_code();
 * }
 *
 * USFSTL_CODE_TEST_CASES(
 *	// multiple arguments (not lack of commas)
 *	TC_ARG(int, arg1)
 *	TC_ARG(int, arg2),
 *	// multiple test cases with data
 *	TC_DATA(.arg1 =  7, .arg2 = 25)
 *	TC_DATA(.arg1 =  8, .arg2 =  0)
 *	TC_DATA(.arg1 = 42, .arg2 =  1),
 *	// you can also assign to .generic for info
 *	.generic.known_failing = true,
 * )
 * {
 *	other_code();
 *	some_code();
 *	another_func(data->arg1, data->arg2);
 * }
 *
 * USFSTL_CODE_TEST_FUNC(...)
 * {
 *	// common code
 *
 *	USFSTL_RUN_CODE_TEST_CASE();
 *
 *	// common code
 * }
 *
 * // this "calls" the given macro type the right function,
 * // extra data, cases data and then the rest of the args
 * USFSTL_CODE_TEST(USFSTL_UNIT_TEST, extra, ...);
 */
struct usfstl_code_testcase {
	struct usfstl_testcase generic;
	void (*fn)(const void *);
	const void *data;
	unsigned int dsz, ndata;
};

#define _TCNAME(tn, n, sfx)		tn##_testcase_line_##n##sfx
#define TCNAME(tn, n, sfx)		_TCNAME(tn, n, sfx)

#define USFSTL_CODE_TEST_CASE(...)		_USFSTL_CODE_TEST_CASE(TEST_NAME, __LINE__, __VA_ARGS__)
#define _USFSTL_CODE_TEST_CASE(tn, ctr, ...)	__USFSTL_CODE_TEST_CASE(tn, ctr, __VA_ARGS__)
#define __USFSTL_CODE_TEST_CASE(tn, ctr, ...)				\
static void TCNAME(tn,ctr,)(const void *data);				\
static const struct usfstl_code_testcase TCNAME(tn,ctr,_data) = {	\
	.fn = TCNAME(tn,ctr,),						\
	.ndata = 1,							\
	__VA_ARGS__							\
};									\
static const struct usfstl_code_testcase * const TCNAME(tn,ctr,_ptr)	\
__attribute__((used,section("usfstl_code_test_cases_" #tn))) =		\
	&TCNAME(tn,ctr,_data);						\
static void TCNAME(tn,ctr,)(const void *data)

#define TC_ARG(tp, n) tp n;
#define TC_DATA(...) { __VA_ARGS__ },
#define USFSTL_CODE_TEST_CASES(_args, _data, ...)			\
	_USFSTL_CODE_TEST_CASES(_args, USFSTL_COUNT(_data),		\
				_TOGETHER(_data), TEST_NAME,		\
				__LINE__, __VA_ARGS__)
#define _USFSTL_CODE_TEST_CASES(_args, _cnt, _data, tn, ctr, ...)	\
	__USFSTL_CODE_TEST_CASES(_args, _cnt,				\
				 _TOGETHER(_data), tn, ctr,		\
				 __VA_ARGS__)
#define __USFSTL_CODE_TEST_CASES(_args, _cnt, _sdata, tn, ctr, ...)	\
struct TCNAME(tn,ctr,_data) { _args };					\
static struct TCNAME(tn,ctr,_data)					\
const TCNAME(tn,ctr,_cases)[] = { _sdata };				\
static void TCNAME(tn,ctr,)(const struct TCNAME(tn,ctr,_data) *data);	\
static const struct usfstl_code_testcase TCNAME(tn,ctr,_data) = {	\
	.fn = (void *)TCNAME(tn,ctr,),					\
	.data = TCNAME(tn,ctr,_cases),					\
	.ndata = sizeof(TCNAME(tn,ctr,_cases)) /			\
			sizeof(TCNAME(tn,ctr,_cases)[0]),		\
	.dsz = sizeof(TCNAME(tn,ctr,_cases)[0]),			\
	__VA_ARGS__							\
};									\
static const struct usfstl_code_testcase * const TCNAME(tn,ctr,_ptr)	\
__attribute__((used,section("usfstl_code_test_cases_" #tn))) =		\
	&TCNAME(tn,ctr,_data);						\
static void TCNAME(tn,ctr,)(const struct TCNAME(tn,ctr,_data) *data)

#define USFSTL_CODE_TEST_FUNC(...) _USFSTL_CODE_TEST_FUNC(TEST_NAME, __VA_ARGS__)
#define _USFSTL_CODE_TEST_FUNC(tn, ...) __USFSTL_CODE_TEST_FUNC(tn, __VA_ARGS__)
#define __USFSTL_CODE_TEST_FUNC(tn, ...)				\
static void tn(struct usfstl_test *test, void *testcase);		\
extern struct usfstl_code_testcase					\
* __start_usfstl_code_test_cases_##tn[0],				\
* __stop_usfstl_code_test_cases_##tn;					\
static void *tn##_generator(struct usfstl_test *t, unsigned int n)	\
{									\
	struct usfstl_code_testcase **iter, *tc;			\
	static struct usfstl_code_testcase ret = {};			\
	unsigned int i = 0;						\
									\
	iter = &__start_usfstl_code_test_cases_##tn[0];			\
									\
	while (iter < &__stop_usfstl_code_test_cases_##tn) {		\
		tc = *iter;						\
		if (!tc)						\
			continue;					\
		if (n >= i && n < i + tc->ndata) {			\
			unsigned int subcase = n - i;			\
			unsigned int offs = tc->dsz * subcase;		\
									\
			ret = *tc;					\
			ret.data = ((unsigned char *)tc->data) + offs;	\
									\
			return &ret;					\
		}							\
									\
		i += tc->ndata;						\
		iter++;							\
	}								\
									\
	return NULL;							\
}									\
static void tn(struct usfstl_test *test, void *testcase)

static inline void
usfstl_run_code_test_case(const struct usfstl_code_testcase *testcase)
{
	testcase->fn(testcase->data);
}
#define USFSTL_RUN_CODE_TEST_CASE() usfstl_run_code_test_case(testcase)

#define __USFSTL_CODE_TEST_INIT(type, tn, extra, ...)				\
/* declare them so this can be used before USFSTL_CODE_TEST_FUNC() */		\
static void tn(struct usfstl_test *test, void *testcase);			\
static void *tn##_generator(struct usfstl_test *t, unsigned int n);		\
type(tn, extra, NO_CASES, .case_generator = tn##_generator,			\
	.case_generator_has_generic = true,					\
	.testcase_generic_offset =						\
		(int)((char *)&((struct usfstl_code_testcase *)0)->generic -	\
				(char *)0),					\
	__VA_ARGS__)
#define _USFSTL_CODE_TEST_INIT(type, tn, extra, ...)				\
	__USFSTL_CODE_TEST_INIT(type, tn, extra, __VA_ARGS__)
#define USFSTL_CODE_TEST(type, extra, ...)					\
	_USFSTL_CODE_TEST_INIT(type, TEST_NAME, extra, __VA_ARGS__)

/*
 * Call this function after testing a requirement, stating whether
 * it passed/failed. Use this for requirements that aren't directly
 * attached to a test using the REQUIREMENTS() macro.
 */
void usfstl_tested_requirement(const char *req, bool pass);

/*
 * Call this function if the negative test hit the assert you wanted.
 */
void usfstl_negative_test_succeeded(void);

enum usfstl_static_reference_type {
	USFSTL_STATIC_REFERENCE_FUNCTION = 0,
	USFSTL_STATIC_REFERENCE_VARIABLE
};

/*
 * Using this, you can call even a static function in the unit test.
 *
 * Let's say your tested module contains:
 *
 *   static void my_function(int cmd, void *data);
 *
 * Then you can write:
 *
 *   USFSTL_STATIC_TEST_FN(void, my_function, int, void *);
 *
 * in the test case, and call the function as normal:
 *
 *   my_function(7, NULL);
 */
struct usfstl_static_reference {
	void **ptr;
	const char *name;
	const char *filename;
	enum usfstl_static_reference_type reference_type;
};

#define USFSTL_STATIC_TEST_REFERENCE(_name, _reference_type, _filename)					\
	static const struct usfstl_static_reference usfstl_static_reference_##_name = {			\
		.ptr = (void *)&_name,									\
		.name = #_name,										\
		.filename = _filename,									\
		.reference_type = _reference_type							\
	};												\
	static const struct usfstl_static_reference * const usfstl_static_referencep_##_name		\
	__attribute__((used, section("static_reference_data"))) =					\
	&usfstl_static_reference_##_name



#define USFSTL_STATIC_TEST_FN(ret, _name, ...)					\
	static ret (*_name)(__VA_ARGS__);					\
	USFSTL_STATIC_TEST_REFERENCE(_name, USFSTL_STATIC_REFERENCE_FUNCTION, NULL)

/*
 * This macro provides access to static module variables. If for example a module
 * has a static variable named some_static_variable:
 *    static int some_static_variable = 3;
 *
 * Then the test code can gain access to this variable using the following:
 *   USFSTL_STATIC_TEST_VARIABLE(int, some_static_variable, source_file_containing_variable)
 *   *some_static_variable = 5; // Changes the value of the static variable to 5
 *
 * Note that some_static_variable defined by USFSTL_STATIC_TEST_VARIABLE is a pointer
 * type, pointing to the actual static data. No type verification is currently done on
 * the parameters passed to the macro.
 */

#define USFSTL_STATIC_TEST_VARIABLE(_type, _name, _filename)				\
	static _type *_name;								\
	USFSTL_STATIC_TEST_REFERENCE(_name, USFSTL_STATIC_REFERENCE_VARIABLE, _filename)



/*
 * If you need to call the original function of a function that's
 * already stubbed, you can use this macro to get a function pointer
 * that you can use to call it; or if you previously got a pointer to
 * it, use USFSTL_CALL_ORIG_PTR(), for example with USFSTL_STATIC_TEST_FN().
 *
 * This can also be used to implement pre/post stubbing, by stubbing
 * a function and then calling the original within the stub.
 *
 * Usage:
 *   ret = USFSTL_CALL_ORIG(my_function)(arguments);
 *   ret = USFSTL_CALL_ORIG_PTR(fn_ptr)(arguments);
 *
 * This works by just making a function pointer to beyond the __fentry__
 * call generated by the compiler.
 */
#define USFSTL_CALL_ORIG(call)						\
	({								\
		USFSTL_ASSERT_EQ(*(unsigned char *)call, 0xe8, "%x");	\
		((typeof(&call))((unsigned char *)call + 5));		\
	})
#define USFSTL_CALL_ORIG_PTR(call)					\
	({								\
		USFSTL_ASSERT_EQ(*(unsigned char *)call, 0xe8, "%x");	\
		((typeof(call))((unsigned char *)call + 5));		\
	})

/*
 * This may help for stack range checking, points to the address
 * of the stack when starting text execution.
 *
 * Note that if you create threads manually (not via usfstlctx.h) then
 * you're responsible for initializing this in your threads using the
 * usfstl_set_stack_start() function.
 *
 * The underlying implementation will be a __thread variable only if
 * you use usfstl's thread task model, so don't use this for real threads
 * if you don't do that.
 */
#define g_usfstl_stack_start usfstl_get_stack_start()

void *usfstl_get_stack_start(void);
void usfstl_set_stack_start(void *p);

/*
 * Reset (extend) the watchdog, the new timeout is from the current
 * point in time.
 */
void usfstl_watchdog_reset(unsigned int timeout_ms);

/*
 * Dump the stack at this point - good as a debug helper.
 */
void usfstl_dump_stack(void);

/*
 * Give this a pointer to a pointer that will have the data,
 * and a pointer to the length; just call it and use the data
 * e.g. like
 *   usfstl_fuzz(&dataptr, &datalen);
 *   my_fuzzing_function(dataptr, datalen);
 */
void usfstl_fuzz(const unsigned char **buf, size_t *len);

/*
 * Mark a fuzz test as being successful - in case we were forked
 * by AFL this will just exit(0) for speed reasons, but in other
 * cases (e.g. fuzz reproduction) will just return immediately.
 */
void usfstl_fuzz_test_ok(void);

/**
 * USFSTL_INITIALIZER - call a function at binary initialization time
 */
typedef void (*usfstl_init_fn_t)(void);
#define USFSTL_INITIALIZER(fn)						\
static const usfstl_init_fn_t usfstl_init_fn_##fn			\
	__attribute__((used, section("usfstl_initfns"))) = (fn)

/*
 * Helper - binary name
 */
extern const char *g_usfstl_program_name;

#endif // _USFSTL_TEST_H_
