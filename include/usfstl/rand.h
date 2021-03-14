#ifndef _USFSTL_RAND_H_
#define _USFSTL_RAND_H_

#include <stdint.h>

/**
 * usfstl_rand - generate and retrieve a random 32 bit integer
 */
int32_t usfstl_rand(void);

/**
 * usfstl_rand_range - generate and retrieve a random value in the range [min, max]
 * @min: range start
 * @max: range end (inclusive)
 */
int32_t usfstl_rand_range(int32_t min, int32_t max);

#endif // _USFSTL_RAND_H_
