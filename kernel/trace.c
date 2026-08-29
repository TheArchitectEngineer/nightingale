#include <assert.h>
#include <errno.h>
#include <ng/syscall.h>
#include <ng/thread.h>
#include <ng/trace.h>

static void wake_tracer_with(struct thread *tracee, int value);

bool trace_is_stopped(struct thread *th) {
	return (th->trace_state == TRACE_SYSCALL_ENTER_STOP
		|| th->trace_state == TRACE_SYSCALL_EXIT_STOP
		|| th->trace_state == TRACE_SIGNAL_DELIVERY_STOP
		|| th->trace_state == TRACE_TRAPPED);
}

static sysret trace_traceme() {
	struct process *parent = running_process->parent;
	running_thread->trace_state = TRACE_RUNNING;
	running_thread->tracer = parent;
	list_append(&parent->tracees, &running_addr()->trace_node);
	return 0;
}

static sysret trace_attach(struct thread *th) {
	if (!th)
		return -ESRCH;
	th->tracer = running_process;
	th->trace_state = TRACE_RUNNING;
	list_append(&running_process->tracees, &th->trace_node);
	return 0;
}

static sysret trace_getregs(struct thread *th, void *data) {
	if (!th)
		return -ESRCH;
	if (!trace_is_stopped(th))
		return -EINVAL;
	memcpy(data, th->user_ctx, sizeof(interrupt_frame));
	return 0;
}

static sysret trace_setregs(struct thread *th, void *data) {
	if (!th)
		return -ESRCH;
	if (!trace_is_stopped(th))
		return -EINVAL;
	memcpy(th->user_ctx, data, sizeof(interrupt_frame));
	return 0;
}

static sysret trace_start(struct thread *th, enum trace_state ns, int signal) {
	if (!th)
		return -ESRCH;
	bool should_start = trace_is_stopped(th);
	bool in_signal = th->trace_state == TRACE_SIGNAL_DELIVERY_STOP;

	th->trace_state = ns;

	/* When you continue a traced thread, you may pass a signal that will
	 * be delivered to that thread. If the thread is stopped in signal
	 * delivery stop, it will live replace the recieved signal (or remove
	 * it).
	 * Otherwise, I just add the signal to the pending set. This may not
	 * technically be the correct behavior if there are already pending
	 * signals, but it sure beats longjmping directly to handle signal
	 * out of nowhere, which is the only alternative that comes to mind.
	 */
	if (in_signal) {
		th->trace_signal = signal;
	} else {
		if (signal)
			sigaddset(&th->sig_pending, signal);
	}

	if (ns == TRACE_SINGLESTEP) {
		th->user_ctx->flags |= TRAP_FLAG;
	} else {
		th->user_ctx->flags &= ~TRAP_FLAG;
	}

	if (should_start) {
		sched_wake(th);
	}
	return 0;
}

static sysret trace_detach(struct thread *th) {
	if (!th)
		return -ESRCH;
	list_remove(&th->trace_node);
	th->tracer = nullptr;
	return trace_start(th, TRACE_RUNNING, 0);
}

sysret sys_trace(enum trace_command cmd, pid_t pid, void *addr, void *data) {
	struct thread *th = thread_by_id(pid);
	int d_signal = (int)(intptr_t)data; // fun warnings

	switch (cmd) {
	case TR_TRACEME:
		return trace_traceme();
	case TR_ATTACH:
		return trace_attach(th);
	case TR_GETREGS:
		return trace_getregs(th, data);
	case TR_SETREGS:
		return trace_setregs(th, data);
	case TR_READMEM:
		return -ETODO;
	case TR_WRITEMEM:
		return -ETODO;
	case TR_SINGLESTEP:
		return trace_start(th, TRACE_SINGLESTEP, d_signal);
	case TR_SYSCALL:
		return trace_start(th, TRACE_SYSCALL, d_signal);
	case TR_CONT:
		return trace_start(th, TRACE_RUNNING, d_signal);
	case TR_DETACH:
		return trace_detach(th);
	}
	return -EINVAL;
}

static void wake_tracer_with(struct thread *tracee, int value) {
	struct process *tracer = tracee->tracer;
	if (!tracer)
		return;

	tracee->trace_report = value;

	spin_lock(&tracer->wait_lock);
	wq_wake_all_locked(&tracer->wait_wq);
	spin_unlock(&tracer->wait_lock);

	signal_send_proc(tracer, SIGCHLD);
}

void trace_syscall_entry(struct thread *tracee, int syscall) {
	if (tracee->trace_state == TRACE_RUNNING)
		return;

	int report = TRACE_SYSCALL_ENTRY | syscall;

	tracee->trace_state = TRACE_SYSCALL_ENTER_STOP;
	wake_tracer_with(tracee, report);

	sched_block();
}

void trace_syscall_exit(struct thread *tracee, int syscall) {
	if (tracee->trace_state == TRACE_RUNNING)
		return;

	int report = TRACE_SYSCALL_EXIT | syscall;

	tracee->trace_state = TRACE_SYSCALL_EXIT_STOP;
	wake_tracer_with(tracee, report);

	sched_block();
}

int trace_signal_delivery(int signal, sighandler_t handler) {
	struct thread *tracee = running_addr();
	if (!running_thread->tracer)
		return signal;
	int report = TRACE_SIGNAL | signal;

	tracee->trace_state = TRACE_SIGNAL_DELIVERY_STOP;
	wake_tracer_with(tracee, report);

	sched_block();

	return tracee->trace_signal;
}

void trace_report_trap(int interrupt) {
	assert(running_thread->tracer);

	struct thread *tracee = running_addr();
	int report = TRACE_TRAP | interrupt;

	tracee->trace_state = TRACE_TRAPPED;
	wake_tracer_with(tracee, report);

	sched_block();
}
