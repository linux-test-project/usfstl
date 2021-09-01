#ifndef _USFSTL_MACROS_H
#define _USFSTL_MACROS_H

#define USFSTL_BUILD_BUG_ON(expr)	extern void __bbo_##__LINE__(char[1 - 2*!!(expr)])

#if defined(__clang__)
#define USFSTL_IGNORE_OVERRIDE_INIT(...)				\
	_Pragma("clang diagnostic push")				\
	_Pragma("clang diagnostic ignored \"-Winitializer-overrides\"")	\
	__VA_ARGS__							\
	_Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#define USFSTL_IGNORE_OVERRIDE_INIT(...)				\
	_Pragma("GCC diagnostic push")					\
	_Pragma("GCC diagnostic ignored \"-Woverride-init\"")		\
	__VA_ARGS__							\
	_Pragma("GCC diagnostic pop")
#else
#error "Unkown compiler"
#endif

#define _USFSTL_2STR(x)			#x
#define USFSTL_2STR(x)			_USFSTL_2STR(x)

#define __USFSTL_COUNTDOWN		19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
#define __USFSTL_COUNT(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,N, ...)	N
#define _USFSTL_COUNT(...)		__USFSTL_COUNT(__VA_ARGS__)
#define USFSTL_COUNT(...)		_USFSTL_COUNT(, ##__VA_ARGS__, __USFSTL_COUNTDOWN)
#define __USFSTL_ASSERT_HASARGS_LIST	1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,0
#define USFSTL_HASARGS(...)		_USFSTL_COUNT(, ##__VA_ARGS__, __USFSTL_ASSERT_HASARGS_LIST)

#define __usfstl_dispatch(func, numargs) \
	func ## numargs
#define _usfstl_dispatch(func, numargs) \
	__usfstl_dispatch(func, numargs)
#define usfstl_dispatch_count(func, ...) \
	_usfstl_dispatch(func, USFSTL_COUNT(__VA_ARGS__))
#define usfstl_dispatch_has(func, ...) \
	_usfstl_dispatch(func, USFSTL_HASARGS(__VA_ARGS__))

#define USFSTL_MAP_0(fn, pfx, _empty)
#define USFSTL_MAP_1(fn, pfx, _1)	fn(pfx _1)
#define USFSTL_MAP_2(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_1(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_3(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_2(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_4(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_3(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_5(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_4(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_6(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_5(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_7(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_6(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_8(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_7(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_9(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_8(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_10(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_9(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_11(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_10(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_12(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_11(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_13(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_12(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_14(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_13(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_15(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_14(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_16(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_15(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_17(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_16(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_18(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_17(fn, pfx, __VA_ARGS__)
#define USFSTL_MAP_19(fn, pfx, _1, ...)	USFSTL_MAP_1(fn, pfx, _1)USFSTL_MAP_18(fn, pfx, __VA_ARGS__)

#define __SELECT(_drop, ...)	__VA_ARGS__
#define _SELECT(...)		__SELECT(__VA_ARGS__)
#define _REMOVE(...)
#define _SELECT_NOPARENS0(...)
#define _SELECT_NOPARENS_REMOVE
#define _SELECT_NOPARENS1(...)	, _SELECT_NOPARENS ## __VA_ARGS__
#define _SELECT_NOPARENS(...)	usfstl_dispatch_has(_SELECT_NOPARENS, __VA_ARGS__)(__VA_ARGS__)

/*
 * With this, you can do something like having a macro M() that can be invoked
 * like
 *	M(r1, r2, r3, B(b1, b2, b3), C(c1, c2, c3))
 *
 * and expands to, for example
 *	b = { b1, b2, b3 };
 *	c = ( c1, c2, c3 };
 *	m = { r1, r2, r3 };
 *
 * You have to define the wrapper macros:
 *
 *	#define B(...) (_B(__VA_ARGS__))
 *	#define C(...) (_C(__VA_ARGS__))
 *
 * (Note the surrounding parentheses here - this is how we can detect that
 * these were used at all.)
 *
 * And the selectors to use later:
 *
 *	#define _SEL_B(...)	, __SEL_B ## __VA_ARGS__
 *	#define _SEL_C(...)	, __SEL_C ## __VA_ARGS__
 *
 * and then all combinations that should be used or not:
 *
 *	#define __SEL_B_B(...)	b = { __VA_ARGS__ };
 *	#define __SEL_B_C(...)
 *	#define __SEL_C_B(...)
 *	#define __SEL_C_C(...)	c = { __VA_ARGS__ };
 *
 * and then M can be defined:
 *
 *	#define M(...)						\
 *		USFSTL_MAP(_SELECT, _SEL_B, __VA_ARGS__)	\
 *		USFSTL_MAP(_SELECT, _SEL_C, __VA_ARGS__)	\
 *		m = { USFSTL_MAP(_SELECT_NOPARENS, _REMOVE, __VA_ARGS__) }
 *
 *
 * There are few key tricks here:
 *  1) The parentheses *around* the expansion of B() and C(). These
 *     lead to _SEL_B/_SEL_C to be expanded or not.
 *  2) If it is expanded, the second key trick is the leading comma
 *     in the expansion, which leads to _SELECT to actually use it,
 *     as it drops the first argument entirely.
 *  3) So far that has selected for with/without a wrapper macro like
 *     B() or C() (vs. 'bare' args like r1, r2, r3). The next trick
 *     is in defining all possible combinations (__SEL_X_Y) to use or
 *     not the argument. In the above example, only the _X_X are used
 *     but this is obviously not required.
 *  4) The "NOPARENS" part works in the opposite way in conjunction
 *     with _REMOVE being concatenated to drop things entirely.
 *
 * See test.h for a real example of using USFSTL_MAP(_SELECT, ...).
 * There, you can also see how not only _X_X is non-empty.
 */
#define USFSTL_MAP(fn, pfx, ...)	usfstl_dispatch_count(USFSTL_MAP_, __VA_ARGS__)(fn, pfx, __VA_ARGS__)

#endif /* _USFSTL_MACROS_H */
