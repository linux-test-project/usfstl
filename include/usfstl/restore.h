/*
 * Copyright (C) 2019 - 2021 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USFSTL_RESTORE_H_
#define _USFSTL_RESTORE_H_

struct usfstl_restore_info {
	uintptr_t ptr, size;
};

/**
 * usfstl_read_restore_info - read (global) variable info from file
 * @file: path to a 'globals' information file which contains addr and
 *	size for each global symbol
 *
 * This function reads the file and returns a restoration info based on it.
 * The returned pointer should be freed (using regular free()) after use.
 */
struct usfstl_restore_info *usfstl_read_restore_info(const char *file);

/**
 * usfstl_save_restore_data - save and return data to restore variables
 * @info: restoration info obtained from usfstl_read_restore_info()
 *
 * Using the restoration info, this function saves the relevant data to
 * be later restored using usfstl_restore_data().
 * The returned pointer to the data should be freed (using regular free())
 * after use.
 */
void *usfstl_save_restore_data(struct usfstl_restore_info *info);

/**
 * usfstl_restore_data - restore variables to their saved state
 * @info: restore variable information from usfstl_read_restore_info()
 * @restore_data: data saved by usfstl_save_restore_data()
 *
 * Restore all the globals in the @info list to their original state
 * saved in @restore_data.
 */
void usfstl_restore_data(struct usfstl_restore_info *info,
			 const void *restore_data);

#endif /* _USFSTL_RESTORE_H_ */
