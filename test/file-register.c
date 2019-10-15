/*
 * Description: run various file registration tests
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "liburing.h"

static int no_update = 0;

static void close_files(int *files, int nr_files, int add)
{
	char fname[32];
	int i;

	for (i = 0; i < nr_files; i++) {
		if (files)
			close(files[i]);
		if (!add)
			sprintf(fname, ".reg.%d", i);
		else
			sprintf(fname, ".add.%d", i + add);
		unlink(fname);
	}
	if (files)
		free(files);
}

static int *open_files(int nr_files, int extra, int add)
{
	char fname[32];
	int *files;
	int i;

	files = calloc(nr_files + extra, sizeof(int));

	for (i = 0; i < nr_files; i++) {
		if (!add)
			sprintf(fname, ".reg.%d", i);
		else
			sprintf(fname, ".add.%d", i + add);
		files[i] = open(fname, O_RDWR | O_CREAT, 0644);
		if (files[i] < 0) {
			perror("open");
			free(files);
			files = NULL;
			break;
		}
	}
	if (extra) {
		for (i = nr_files; i < nr_files + extra; i++)
			files[i] = -1;
	}

	return files;
}

static int test_shrink(struct io_uring *ring)
{
	int ret, off, fd;
	int *files;

	files = open_files(50, 0, 0);
	ret = io_uring_register_files(ring, files, 50);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	off = 0;
	do {
		fd = -1;
		ret = io_uring_register_files_update(ring, off, &fd, 1);
		if (ret != 1) {
			if (off == 50 && ret == -EINVAL)
				break;
			fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
			break;
		}
		off++;
	} while (1);

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	close_files(files, 50, 0);
	return 0;
err:
	close_files(files, 50, 0);
	return 1;
}


static int test_grow(struct io_uring *ring)
{
	int ret, off;
	int *files, *fds = NULL;

	files = open_files(50, 250, 0);
	ret = io_uring_register_files(ring, files, 300);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	off = 50;
	do {
		fds = open_files(1, 0, off);
		ret = io_uring_register_files_update(ring, off, fds, 1);
		if (ret != 1) {
			if (off == 300 && ret == -EINVAL)
				break;
			fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
			break;
		}
		if (off >= 300) {
			fprintf(stderr, "%s: Succeeded beyond end-of-list?\n", __FUNCTION__);
			goto err;
		}
		off++;
	} while (1);

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	close_files(files, 100, 0);
	close_files(NULL, 251, 50);
	return 0;
err:
	close_files(files, 100, 0);
	close_files(NULL, 251, 50);
	return 1;
}

static int test_replace_all(struct io_uring *ring)
{
	int *files, *fds = NULL;
	int ret, i;

	files = open_files(100, 0, 0);
	ret = io_uring_register_files(ring, files, 100);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	fds = malloc(100 * sizeof(int));
	for (i = 0; i < 100; i++)
		fds[i] = -1;

	ret = io_uring_register_files_update(ring, 0, fds, 100);
	if (ret != 100) {
		fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	close_files(files, 100, 0);
	if (fds)
		free(fds);
	return 0;
err:
	close_files(files, 100, 0);
	if (fds)
		free(fds);
	return 1;
}

static int test_replace(struct io_uring *ring)
{
	int *files, *fds = NULL;
	int ret;

	files = open_files(100, 0, 0);
	ret = io_uring_register_files(ring, files, 100);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	fds = open_files(10, 0, 1);
	ret = io_uring_register_files_update(ring, 90, fds, 10);
	if (ret != 10) {
		fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	close_files(files, 100, 0);
	if (fds)
		close_files(fds, 10, 1);
	return 0;
err:
	close_files(files, 100, 0);
	if (fds)
		close_files(fds, 10, 1);
	return 1;
}

static int test_removals(struct io_uring *ring)
{
	int *files, *fds = NULL;
	int ret, i;

	files = open_files(100, 0, 0);
	ret = io_uring_register_files(ring, files, 100);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	fds = calloc(10, sizeof(int));
	for (i = 0; i < 10; i++)
		fds[i] = -1;

	ret = io_uring_register_files_update(ring, 50, fds, 10);
	if (ret != 10) {
		fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	close_files(files, 100, 0);
	if (fds)
		free(fds);
	return 0;
err:
	close_files(files, 100, 0);
	if (fds)
		free(fds);
	return 1;
}

static int test_additions(struct io_uring *ring)
{
	int *files, *fds = NULL;
	int ret;

	files = open_files(100, 100, 0);
	ret = io_uring_register_files(ring, files, 200);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	fds = open_files(2, 0, 1);
	ret = io_uring_register_files_update(ring, 100, fds, 2);
	if (ret != 2) {
		fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	close_files(files, 100, 0);
	if (fds)
		close_files(fds, 2, 1);
	return 0;
err:
	close_files(files, 100, 0);
	if (fds)
		close_files(fds, 2, 1);
	return 1;
}

static int test_sparse(struct io_uring *ring)
{
	int *files;
	int ret;

	files = open_files(100, 100, 0);
	ret = io_uring_register_files(ring, files, 200);
	if (ret) {
		if (ret == -EBADF) {
			fprintf(stdout, "Sparse files not supported\n");
			no_update = 1;
			goto done;
		}
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}
	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}
done:
	close_files(files, 100, 0);
	return 0;
err:
	close_files(files, 100, 0);
	return 1;
}

static int test_basic_many(struct io_uring *ring)
{
	int *files;
	int ret;

	files = open_files(768, 0, 0);
	ret = io_uring_register_files(ring, files, 768);
	if (ret) {
		fprintf(stderr, "%s: register %d\n", __FUNCTION__, ret);
		goto err;
	}
	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister %d\n", __FUNCTION__, ret);
		goto err;
	}
	close_files(files, 768, 0);
	return 0;
err:
	close_files(files, 768, 0);
	return 1;
}

static int test_basic(struct io_uring *ring)
{
	int *files;
	int ret;

	files = open_files(100, 0, 0);
	ret = io_uring_register_files(ring, files, 100);
	if (ret) {
		fprintf(stderr, "%s: register %d\n", __FUNCTION__, ret);
		goto err;
	}
	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister %d\n", __FUNCTION__, ret);
		goto err;
	}
	close_files(files, 100, 0);
	return 0;
err:
	close_files(files, 100, 0);
	return 1;
}

/*
 * Register 0 files, but reserve space for 10.  Then add one file.
 */
static int test_zero(struct io_uring *ring)
{
	int *files, *fds = NULL;
	int ret;

	files = open_files(0, 10, 0);
	ret = io_uring_register_files(ring, files, 10);
	if (ret) {
		fprintf(stderr, "%s: register ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	fds = open_files(1, 0, 1);
	ret = io_uring_register_files_update(ring, 0, fds, 1);
	if (ret != 1) {
		fprintf(stderr, "%s: update ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	ret = io_uring_unregister_files(ring);
	if (ret) {
		fprintf(stderr, "%s: unregister ret=%d\n", __FUNCTION__, ret);
		goto err;
	}

	if (fds)
		close_files(fds, 1, 1);
	free(files);
	return 0;
err:
	if (fds)
		close_files(fds, 1, 1);
	free(files);
	return 1;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	int ret;

	ret = io_uring_queue_init(8, &ring, 0);
	if (ret) {
		printf("ring setup failed\n");
		return 1;
	}

	ret = test_basic(&ring);
	if (ret) {
		printf("test_basic failed\n");
		return ret;
	}

	ret = test_basic_many(&ring);
	if (ret) {
		printf("test_basic_many failed\n");
		return ret;
	}

	ret = test_sparse(&ring);
	if (ret) {
		printf("test_sparse failed\n");
		return ret;
	}

	if (no_update)
		return 0;

	ret = test_additions(&ring);
	if (ret) {
		printf("test_additions failed\n");
		return ret;
	}

	ret = test_removals(&ring);
	if (ret) {
		printf("test_removals failed\n");
		return ret;
	}

	ret = test_replace(&ring);
	if (ret) {
		printf("test_replace failed\n");
		return ret;
	}

	ret = test_replace_all(&ring);
	if (ret) {
		printf("test_replace_all failed\n");
		return ret;
	}

	ret = test_grow(&ring);
	if (ret) {
		printf("test_grow failed\n");
		return ret;
	}

	ret = test_shrink(&ring);
	if (ret) {
		printf("test_shrink failed\n");
		return ret;
	}

	ret = test_zero(&ring);
	if (ret) {
		printf("test_zero failed\n");
		return ret;
	}

	return 0;
}