add_subdirectory(dwarf)

target_sources(usfstl PRIVATE
    dwarf-rpc.h
    internal.h
    multi-rpc.h
    print-rpc.h
    rpc-rpc.h
)

target_sources(usfstl PRIVATE
    print.c
    main.c
    override.c
    dwarf.c
    testrun.c
    restore.c
    fuzz.c
    opt.c
    ctx-${USFSTL_CONTEXT_BACKEND}.c
    ctx-common.c
    sched.c
    task.c
    rpc.c
    multi.c
    multi-rpc.c
    multi-ctl.c
    multi-ptc.c
    multi-shared-mem.c
    rpc-rpc.c
    loop.c
    alloc.c
    assert-profiling.c
    string.c
    rand.c
    entry.S
)

if (windows)
    target_sources(usfstl PRIVATE
        watchdog-win32.c
        rpc-win32.c
        multi-win32.c
    )
    return()
endif()

target_sources(usfstl PRIVATE
    watchdog-posix.c
    rpc-posix.c
    multi-posix.c
    wallclock.c
)

if (USFSTL_VHOST_USER)
    target_sources(usfstl PRIVATE
        vhost.c
        uds.c
        pci.c
    )
endif()

if (USFSTL_SCHED_CTRL)
    target_sources(usfstl PRIVATE
        uds.c
        schedctrl.c
        ctrluds.c
    )
endif()
