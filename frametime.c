#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <GL/glx.h>

static void (*realswap)(Display *dpy, GLXDrawable drawable) = NULL;

#define PREFIX "libframetime: "

static FILE *f = NULL;
static struct timeval oldtime;
static unsigned char firstdone = 0;

void glXSwapBuffers(Display * const dpy, GLXDrawable drawable) {

	realswap(dpy, drawable);
}

static void die(const char fmt[], ...) {

	va_list ap;

	va_start(ap, fmt);

	vfprintf(stderr, fmt, ap);

	va_end(ap);
	exit(1);
}

static void init() __attribute__((constructor));
static void deinit() __attribute__((destructor));

static void init() {

	const char * const env = getenv("LIBFRAMETIME_FILE");
	const char * const name = env ? env : "/tmp/libframetime.out";

	f = fopen(name, "w");

	if (!f) {
		die(PREFIX "Failed to open %s for writing\n",
			name);
	}

	dlerror();
	realswap = dlsym(RTLD_NEXT, "glXSwapBuffers");

	const char * const err = dlerror();
	if (err) {
		die(PREFIX "dlsym failed: %s\n", err);
	}
}

static void deinit() {

	fflush(f);
	fsync(fileno(f));
	fclose(f);
}
