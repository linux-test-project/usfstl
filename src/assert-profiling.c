/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <usfstl/uthash.h>
#include <usfstl/test.h>
#include <usfstl/opt.h>
#include "internal.h"

char *g_usfstl_assert_coverage_file;
USFSTL_OPT_STR("log-assert-coverage", 0, "filename", g_usfstl_assert_coverage_file,
	       "log assert coverage information after each test to this file");

static int g_assert_coverage_out_fd;

static struct usfstl_assert_profiling_info *USFSTL_NORESTORE_VAR(assert_info_hash);

#define for_each_hash_entry(current, tmp) HASH_ITER(hh, assert_info_hash, current, tmp)

extern struct usfstl_assert_profiling_info *__start_usfstl_assert_profiling_info[];
extern struct usfstl_assert_profiling_info *__stop_usfstl_assert_profiling_info;

/*
 * The comma in the initialization part does not function as a statement
 * delimiter, but rather as a declaration delimiter. Thus, **pp is not a
 * dereference.
 */
#define for_each_assert_info_in_section(_iter)				\
	for (int i = 0;							\
	     &__start_usfstl_assert_profiling_info[i] <			\
		&__stop_usfstl_assert_profiling_info;			\
	     i++)							\
		if ((_iter = __start_usfstl_assert_profiling_info[i]))

static void clear_assert_info_hash_and_counters(void)
{
	struct usfstl_assert_profiling_info *iter;

	for_each_assert_info_in_section(iter)
		iter->count = 0;

	HASH_CLEAR(hh, assert_info_hash);
	assert_info_hash = NULL;
}

static void calculate_key(struct usfstl_assert_profiling_info *assert_info_item)
{
	uint32_t bytes_needed = snprintf(assert_info_item->key, sizeof(assert_info_item->key),
					"%s:%d:%s", assert_info_item->file,
					assert_info_item->line, assert_info_item->condition);
	USFSTL_ASSERT(bytes_needed < sizeof(assert_info_item->key));
}

// Dedup assert_profiling_entry structs.
// Has the side effect of potentially adding the provided entry to the assert_info hash.
static struct usfstl_assert_profiling_info*
get_deduped_assert_info_entry(struct usfstl_assert_profiling_info *entry)
{
	struct usfstl_assert_profiling_info *deduped_entry;

	calculate_key(entry);
	HASH_FIND_STR(assert_info_hash, entry->key, deduped_entry);
	if (!deduped_entry) {
		HASH_ADD_STR(assert_info_hash, key, entry); /* id: name of key field */
		return entry;
	}

	return deduped_entry;
}

static void set_entry_in_assert_hash(struct usfstl_assert_profiling_info *entry)
{
	// Using the side-effect of having this call potentially add the entry to the hash
	get_deduped_assert_info_entry(entry);
}

static void sum_assert_occurrences(void)
{
	struct usfstl_assert_profiling_info *iter, *deduped_entry;

	for_each_assert_info_in_section(iter) {
		deduped_entry = get_deduped_assert_info_entry(iter);
		if (iter != deduped_entry)
			deduped_entry->count += iter->count;
	}
}

void usfstl_init_reached_assert_log(void)
{
	static const char *header = "test_name,testcase_num,assert_file,assert_line,assert_condition,req_fmt,call_count\n";
	int header_length = strlen(header);

	g_assert_coverage_out_fd = open(g_usfstl_assert_coverage_file,
					O_CREAT | O_WRONLY | O_TRUNC,
					0666);

	USFSTL_ASSERT(g_assert_coverage_out_fd >= 0);
	USFSTL_ASSERT_EQ((long)header_length,
			 (long)write(g_assert_coverage_out_fd,
				     header, strlen(header)),
			 "%ld");
}

void usfstl_list_all_asserts(void)
{
	struct usfstl_assert_profiling_info *iter, *tmp;

	for_each_assert_info_in_section(iter)
		set_entry_in_assert_hash(iter);

	printf("filename,line,condition,reqfmt\n");

	for_each_hash_entry(iter, tmp)
		printf("\"%s\",%d,\"%s\",\"%s\"\n",
		       iter->file, iter->line,
		       iter->condition, iter->reqfmt);
}

void usfstl_log_reached_asserts(void)
{
	struct usfstl_assert_profiling_info *iter;
	struct usfstl_assert_profiling_info *tmp;
	char buf[2000];

	sum_assert_occurrences();

	for_each_hash_entry(iter, tmp) {
		long len;

		if (!iter->count)
			continue;

		len = snprintf(buf, sizeof(buf),
			       "\"%s\",%d,\"%s\",%d,\"%s\",\"%s\",%d\n",
			       g_usfstl_current_test->name,
			       g_usfstl_current_case_num,
			       iter->file, iter->line,
			       iter->condition, iter->reqfmt,
			       iter->count);
		USFSTL_ASSERT_EQ((long)write(g_assert_coverage_out_fd, buf, len),
				 len, "%ld");
	}

	clear_assert_info_hash_and_counters();
}
