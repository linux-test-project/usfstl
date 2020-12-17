add-auto-load-safe-path linux/scripts/gdb/
source linux/vmlinux-gdb.py
cd linux
lx-symbols ../driver-install/
cd ..
handle 11 nostop noprint pass
