/*
libframetime, a frame time dumper for all GL applications
Copyright (C) 2013 Lauri Kasanen

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <GL/glx.h>

#ifndef NO_EGL
#include <EGL/egl.h>
#endif

typedef uint8_t u8;
typedef uint32_t u32;

static void (*realswap)(Display *dpy, GLXDrawable drawable) = NULL;
static void *(*realdlsym)(void *handle, const char *symbol) = NULL;

#define PREFIX "libframetime: "

static FILE *f = NULL;
static struct timeval oldtime;
static u8 firstdone = 0;

static void timing() {

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
}

static void die(const char fmt[], ...) {

	va_list ap;

	va_start(ap, fmt);

	vfprintf(stderr, fmt, ap);

	va_end(ap);
	exit(1);
}

static void initdlsym() {

	void *libdl = dlopen("libdl.so", RTLD_LAZY);
	if (!libdl) die(PREFIX "Failed to open libdl.so\n");

	realdlsym = dlvsym(libdl, "dlsym", "GLIBC_2.2.5");
	if (!realdlsym) die(PREFIX "Failed loading dlsym\n");
}

void *dlsym(void *handle, const char *symbol) {
	// High evil wrapping this function.
	if (symbol && !strcmp(symbol, "glXSwapBuffers"))
		return glXSwapBuffers;
	if (symbol && !strcmp(symbol, "eglSwapBuffers"))
		return eglSwapBuffers;

	if (!realdlsym)
		initdlsym();

	return realdlsym(handle, symbol);
}

#ifndef NO_EGL
static EGLBoolean (*realegl)(EGLDisplay display, EGLSurface surface) = NULL;

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {

	timing();

	return realegl(display, surface);
}
#endif

void glXSwapBuffers(Display * const dpy, GLXDrawable drawable) {

	timing();

	realswap(dpy, drawable);
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

	if (!realdlsym)
		initdlsym();

	dlerror();
	realswap = realdlsym(RTLD_NEXT, "glXSwapBuffers");

	const char *err = dlerror();
	if (err) {
		die(PREFIX "dlsym failed: %s\n", err);
	}

#ifndef NO_EGL

	dlerror();
	realegl = realdlsym(RTLD_NEXT, "eglSwapBuffers");

	err = dlerror();
	if (err) {
		die(PREFIX "dlsym failed: %s\n", err);
	}

#endif
}

static void deinit() {

	fflush(f);
	fsync(fileno(f));
	fclose(f);
}
