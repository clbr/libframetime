#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <GL/glx.h>

typedef uint8_t u8;
typedef uint32_t u32;

static void (*realswap)(Display *dpy, GLXDrawable drawable) = NULL;

#define PREFIX "libframetime: "

static FILE *f = NULL;
static struct timeval oldtime;
static u8 firstdone = 0;

void glXSwapBuffers(Display * const dpy, GLXDrawable drawable) {

	if (!firstdone) {
		gettimeofday(&oldtime, NULL);
		firstdone = 1;
	} else {
		struct timeval newtime;
		gettimeofday(&newtime, NULL);

		u32 usec = (newtime.tv_sec - oldtime.tv_sec) * 1000 * 1000;
		usec += newtime.tv_usec - oldtime.tv_usec;

		oldtime = newtime;

		fprintf(f, "Frametime %u us\n", usec);
	}

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
