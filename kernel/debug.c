#include <assert.h>
#include <ng/debug.h>
#include <ng/syscalls.h>

__NOINLINE void break_point() {
	// This is called in assert() to give a place to put a
	// gdb break point
}

sysret sys_fault(enum fault_type type) {
	volatile int *x = nullptr;
	switch (type) {
	case NULL_DEREF:
		return *x;
	case ASSERT:
		assert(0);
		break;
	default:
		return -EINVAL;
	}
}
