target_sources(usfstl PRIVATE
    backtrace.h
    config.h
    filenames.h
    internal.h
)

target_sources(usfstl PRIVATE
    dwarf.c
    sort.c
    state.c
    fileline.c
    posix.c
    backtrace.c
)

if (windows)
    target_sources(usfstl PRIVATE
        pecoff.c
        nounwind.c
        alloc.c
        read.c
    )
else()
    target_sources(usfstl PRIVATE
        elf.c
        mmap.c
        mmapio.c
    )
endif()
