# Contributing to USFSTL

We welcome contributions to this project, you can make them as github
pull requests.

## Issues

Please report issues you find as github tickets.

## Code changes

We welcome changes and extensions to the code assuming they don't
break our own use of the project.

### Coding style

This project (mostly) uses the [Linux coding style guidelines]
(https://www.kernel.org/doc/html/latest/process/coding-style.html).
You can use the kernel's checkpatch.pl to some extent (it will not
know about any special macros used here, etc.)

### Commit messages

We place great value in explanatory commit messages, please try to
explain any changes well in the commit messages. The commit message
should be

 * in active voice (**good**: "fix the feature xyz"; **bad**: "fixed
    feature abc");
 * describe **all** the changes made, even incidental changes should
    be described, and in most cases be avoided (you can e.g. make some
    cleanups as separate commits, if needed);
 * roughly indicate how they were tested.

### Testing

There's an example project that tests some aspects of the code (under
example/), this should run. Additionally, there are some tests in the
tests/ folder, they can be run with "make test". If you add any, please
add the necessary Makefile target to allow that.

There are also examples for the virtual lab in the vlab/ folder, you
can use those to test other changes.

Additionally, we'll run our own projects that use this library with any
changes before accepting them.

### Pull requests

You should send pull requests via the regular github process, we'll
merge them as real merges, so please ensure the history of your pull
requests is clean and each commit describes the intent well.
