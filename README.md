# USFSTL

## Introduction

This is the User Space Firmware Simulation Testing Library ("USFSTL")
-- a simulation (and to some extent unit) testing framework (mostly)
intended for firmware.

The library also integrates with User-Mode-Linux and vhost-user to
allow simulating devices and testing them against a real (virtio)
driver running in Linux. If you came here just for this, take a look
at the vlab/ directory and the code under control/ first.

## License

This library/code is licensed under BSD-3-Clause, see the
[LICENSE](LICENSE) file.

## Contributions

We welcome contributions to this code, please take a look at the
[CONTRIBUTING.md](CONTRIBUTING.md) file.

## Capabilities

The library (and associated tools) offer the following:

### Job Scheduler

There's support for scheduling "jobs", which are really just function
callbacks to occur at a specific point in time. This might be useful
by itself, but is mostly used together with the contexts to provide a
cooperative multi-threading implementation (tasks).

The job scheduler can also be used together with User-Mode-Linux, we
have a global scheduler for the "time-travel=ext:..." protocol that
Linux supports. This tool (under control/) also supports virtio-net
devices using the vhost-user capability.

### Simulation-capable vhost-user support

With a few extensions to the vhost-user protocol that are implemented
here and in the User-Mode-Linux virtio driver, the protocol becomes
capable of doing time-independent simulation.

### Contexts

Contexts are execution contexts, provided either by threads (pthread,
available on Windows and Linux), fibers (Windows) or ucontexts (Linux).
This allows having different pieces of code execute semi-concurrently.
In this, context switching would have to be implemented manually.

### Tasks / Semaphores

This is a full cooperative multi-threading implementation that allows
switching between tasks, locking (semaphores), relative priorities,
and (through interaction between the scheduler and tasks) grouping
tasks and blocking groups for execution, e.g. to implement critical
sections.

### Function stubbing

This allows skipping or replacing any inner function calls that may
be made, in order to test.

### Static function calling

This allows calling a function that's marked static in the tested
code (assuming you make the compiler not inline it.)

### Assert mechanism

There are various assert macros that fail a test and print out a
more or less verbose message, including stack dump.

### Multi-process testing / RPC framework

The RPC framework is geared towards simulation and will automatically
propagate the current time on RPC calls, etc.

### Logging framework

A very basic logging framework exists that can take advantage of the
RPC mechanisms to have central logging when multiple binaries are
involved in a test.

### Test runner

Multiple tests can be linked into the same binary (though they
currently need to have the same tested files, outside of which
function calls are not permitted), and the test runner will run
and summarize the tests, this is provide as a main() function in
the USFSTL library that gets linked with the test cases.

### Global data handling

Global data is saved at the beginning and restored after each test
run to ensure a clean state for the next test.


## Security

We do not expect this library to be used in any context other than
testing, and as such don't expect that there will be any security
issues.

However, should you nevertheless find a security issue, please report
it privately to johannes@sipsolutions.net, rather than filing a github
ticket.
