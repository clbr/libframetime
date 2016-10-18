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

static void *(*real_dlsym)(void *handle, const char *symbol) = NULL;
static __GLXextFuncPtr (*real_glXGetProcAddressARB)(const GLubyte *) = NULL;
static void (*real_glXSwapBuffers)(Display *dpy, GLXDrawable drawable) = NULL;
#ifndef NO_EGL
static EGLBoolean (*real_eglSwapBuffers)(EGLDisplay display, EGLSurface surface) = NULL;
#endif

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
		fflush(f);
	}
}

static void die(const char fmt[], ...) {

	va_list ap;

	va_start(ap, fmt);

	vfprintf(stderr, fmt, ap);

	va_end(ap);
	exit(1);
}

static void init_dlsym() {

	void *libdl = dlopen("libdl.so", RTLD_LAZY);
	if (!libdl) die(PREFIX "Failed to open libdl.so\n");

	real_dlsym = dlvsym(libdl, "dlsym", "GLIBC_2.2.5");
	if (!real_dlsym)
		real_dlsym = dlvsym(libdl, "dlsym", "GLIBC_2.0");

	if (!real_dlsym) die(PREFIX "Failed loading dlsym\n");
}

void *dlsym(void *handle, const char *symbol) {
	// High evil wrapping this function.

#	define RETURN_ON_MATCH(function_name) \
	do { \
		if (symbol && !strcmp(symbol, #function_name)) \
			return function_name; \
	} while (0)

	RETURN_ON_MATCH(glXGetProcAddressARB);
	RETURN_ON_MATCH(glXSwapBuffers);
#	ifndef NO_EGL
	RETURN_ON_MATCH(eglSwapBuffers);
#	endif

#	undef RETURN_ON_MATCH

	if (!real_dlsym)
		init_dlsym();

	return real_dlsym(handle, symbol);
}

__GLXextFuncPtr glXGetProcAddressARB(const GLubyte *name) {

	if (name && !strcmp((char *) name, "glXSwapBuffers"))
		return (__GLXextFuncPtr) glXSwapBuffers;

	return real_glXGetProcAddressARB(name);
}

void glXSwapBuffers(Display * const dpy, GLXDrawable drawable) {

	timing();

	real_glXSwapBuffers(dpy, drawable);
}

#ifndef NO_EGL
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {

	timing();

	return real_eglSwapBuffers(display, surface);
}
#endif

static void init() __attribute__((constructor));
static void deinit() __attribute__((destructor));

static void init() {

	const char * const env = getenv("LIBFRAMETIME_FILE");
	const char * const name = env ? env : "/tmp/libframetime.out";

	f = fopen(name, "w");

	if (!f)
		die(PREFIX "Failed to open %s for writing\n", name);

	if (!real_dlsym)
		init_dlsym();

	const char *err;
	void *lib;

#	define INIT_real_(function_name) \
	do { \
		dlerror(); \
		real_##function_name = real_dlsym(RTLD_NEXT, #function_name); \
		\
		err = dlerror(); \
		if (err) \
			die(PREFIX "dlsym failed: %s\n", err); \
	} while (0)

	// If the app loads libs dynamically, the symbol may be NULL.
#	define INIT_FROM_LIB_real_(function_name, library_name) \
	do { \
		INIT_real_(function_name); \
		\
		if (!real_##function_name) { \
			lib = dlopen(#library_name".so", RTLD_LAZY); \
			if (!lib) \
				die(PREFIX "dynamic "#library_name" failed\n"); \
			real_##function_name = real_dlsym(lib, #function_name); \
			if (!real_##function_name) \
				die(PREFIX "Failed to find "#function_name" in "#library_name".so\n"); \
		} \
	} while (0)

	INIT_FROM_LIB_real_(glXGetProcAddressARB, libGL);
	INIT_FROM_LIB_real_(glXSwapBuffers, libGL);
#	ifndef NO_EGL
	INIT_FROM_LIB_real_(eglSwapBuffers, libEGL);
#	endif

#	undef INIT_FROM_LIB_real_
#	undef INIT_real_
}

static void deinit() {

	fflush(f);
	fsync(fileno(f));
	fclose(f);
}
