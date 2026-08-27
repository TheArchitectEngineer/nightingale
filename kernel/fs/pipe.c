#include <fcntl.h>
#include <ng/fs/file.h>
#include <ng/fs/file_system.h>
#include <ng/fs/pipe.h>
#include <ng/fs/vnode.h>
#include <ng/signal.h>
#include <ng/sync.h>
#include <stdlib.h>
#include <string.h>

#define wait_on wq_block_on
#define wake_from wq_notify_all

struct vnode_ops pipe_ops;
struct file_ops pipe_file_ops;

struct vnode *new_pipe() {
	struct vnode *vnode = new_vnode(initfs_file_system, S_IFIFO | 0777);
	vnode->capacity = 16384;
	vnode->data = malloc(vnode->capacity);
	vnode->ops = &pipe_ops;
	vnode->file_ops = &pipe_file_ops;
	vnode->type = FT_PIPE;
	return vnode;
}

int pipe_open(struct vnode *pipe, struct file *file) {
	if (pipe->is_anon_pipe)
		return 0;

	if (file->flags & O_WRONLY && file->flags & O_RDONLY)
		return -EINVAL;

	if (file->flags & O_WRONLY && pipe->read_refcnt == 0)
		wait_on(&pipe->write_queue);

	if (file->flags & O_RDONLY && pipe->write_refcnt == 0)
		wait_on(&pipe->read_queue);

	if (file->flags & O_WRONLY && pipe->write_refcnt == 0)
		wake_from(&pipe->read_queue);

	if (file->flags & O_RDONLY && pipe->read_refcnt == 0)
		wake_from(&pipe->write_queue);

	return 0;
}

int pipe_close(struct vnode *pipe, struct file *file) {
	if (pipe->write_refcnt == 0)
		wake_from(&pipe->read_queue);
	if (pipe->read_refcnt == 0)
		wake_from(&pipe->write_queue);
	return 0;
}

struct vnode_ops pipe_ops = {
	.open = pipe_open,
	.close = pipe_close,
};

static bool pipe_can_read(struct vnode *v) {
	return v->len > 0 || v->write_refcnt == 0;
}

static size_t pipe_copy_to_user(struct vnode *v, char *user_buffer, size_t len) {
	size_t to_read = MIN(len, v->len);
	memcpy(user_buffer, v->data, to_read);
	memmove(v->data, v->data + to_read, v->len - to_read);
	v->len -= to_read;
	wake_from(&v->write_queue);
	return to_read;
}

static size_t pipe_copy_from_user(struct vnode *v, const char *user_buffer, size_t len) {
	size_t to_write = MIN(len, v->capacity - v->len);
	memcpy(v->data + v->len, user_buffer, to_write);
	v->len += to_write;
	wake_from(&v->read_queue);
	return to_write;
}

ssize_t pipe_read(struct file *file, char *buffer, size_t len) {
	struct vnode *v = file->vnode;
	while (!pipe_can_read(v))
		wait_on(&v->read_queue);

	return pipe_copy_to_user(v, buffer, len);
}

ssize_t pipe_write(struct file *file, const char *buffer, size_t len) {
	struct vnode *v = file->vnode;

	if (!v->read_refcnt) {
		signal_self(SIGPIPE);
		return 0;
	}

	while (v->len == v->capacity && v->read_refcnt)
		wait_on(&v->write_queue);

	if (!v->read_refcnt) {
		signal_self(SIGPIPE);
		return 0;
	}

	return pipe_copy_from_user(v, buffer, len);
}

struct file_ops pipe_file_ops = {
	.read = pipe_read,
	.write = pipe_write,
};
