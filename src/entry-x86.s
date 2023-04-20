//
// Copyright (C) 2018 - 2020 Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause
//
#ifdef __x86_64__
#define REG(_r) %r##_r
#define TMP_REG	%r11
#else
#define REG(_r) %e##_r
#define TMP_REG	%ecx
#endif

.data
.globl _g_usfstl_recurse
_g_usfstl_recurse:
	.int 0
.text
.globl __fentry__
__fentry__:
#ifdef __x86_64__
	// this is the only argument _usfstl_find_repl() cares about
	push %rdi
	mov 8(%rsp), %rdi
	// but it might clobber these registers
	push %rsi
	push %rdx
	push %rcx
	push %r8
	push %r9
#else
	// find_repl wants return pointer as first argument;
	// luckily it's already on the stack
#endif
	call _usfstl_find_repl

	// check return value and abort if no replacement
	test REG(ax),REG(ax)
#ifdef __x86_64__
	// restore all the arguments/registers
	pop %r9
	pop %r8
	pop %rcx
	pop %rdx
	pop %rsi
	pop %rdi
#endif
	je CALL_ORIG

	// pop our return address and jump to the stub
	// our return address was to the original function,
	// so the stub will return to its caller...
	pop TMP_REG
	jmp *REG(ax)
CALL_ORIG:
	// If we're recursing a function that was OK to call
	// the original of, then don't record sub-calls again,
	// we only invoked usfstl_find_repl() above in order to
	// be able to stub some sub-calls of this, but ones
	// that aren't stubbed are OK to call ...
	//
	// Note that usfstl_find_repl() also checks the value of
	// recurse to allow arbitrary functions to be called
	// when it's set.
	//
	// Also note that we can't implement this purely in the
	// C code because that doesn't get invoked when the
	// function call returns, and thus we can't reset the
	// variable back to NULL upon that happening.
	mov (_g_usfstl_recurse),REG(ax)
	test REG(ax),REG(ax)
	jne RETURN

	// pop our return address
	pop TMP_REG
	// pop return address of the caller
	pop REG(ax)
	mov REG(ax),(_g_usfstl_recurse)

	call *TMP_REG
	// return to caller
	mov (_g_usfstl_recurse),TMP_REG
	push TMP_REG
	movl $0,(_g_usfstl_recurse)
RETURN:
	ret
