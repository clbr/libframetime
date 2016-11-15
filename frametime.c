/*
libframetime, a frame time dumper for all GL applications
Copyright (C) 2013 Lauri Kasanen
Copyright (C) 2016 Elias Vanderstuyft

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

#ifdef GL_ARB_timer_query
static PFNGLGETSTRINGIPROC glGetStringi = NULL;
static PFNGLGETQUERYIVPROC glGetQueryiv = NULL;
static PFNGLGENQUERIESPROC glGenQueries = NULL;
static PFNGLQUERYCOUNTERPROC glQueryCounter = NULL;
static PFNGLGETQUERYOBJECTIVPROC glGetQueryObjectiv = NULL;
static PFNGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v = NULL;

static u8 timerqueries_disabled = 0;
static u8 timerqueries_debug = 0;
/*
The following constant must be set high enough such that
after TIMERQUERIES_NUM frames the first query result should be available.
Tested with Skylake and nouveau GTX 760;
run with LIBFRAMETIME_TIMERQUERIES_DEBUG=1 to verify this works (= stderr clear) on your system.
*/
#define TIMERQUERIES_NUM 4
static GLuint timerqueries[TIMERQUERIES_NUM];
static u8 timerqueries_idxinit = 0, timerqueries_idxback = 0, timerqueries_idxfront = 1;
static GLuint64 timerqueries_oldtime;
static GLint timerqueries_numbits = 0;
#else
static const u8 timerqueries_disabled = 1;
#endif

static void timing_cpuclock() {

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

#ifdef GL_ARB_timer_query
static int init_timerqueries() {

#	define INIT_GLEXT_AND_RETURN_ON_NULL(function_type, function_name) \
	do { \
		function_name = (function_type)real_glXGetProcAddressARB((GLubyte *)#function_name); \
		if (!function_name) { \
			fprintf(stderr, PREFIX "Error: function pointer to %s is NULL\n", #function_name); \
			return -1; \
		} \
	} while (0)

	// If this function is not available (v3.0+), GL_ARB_timer_query isn't either (v3.2+).
	INIT_GLEXT_AND_RETURN_ON_NULL(PFNGLGETSTRINGIPROC, glGetStringi);

	const GLubyte *extension;
	int i = 0;
	while ((extension = glGetStringi(GL_EXTENSIONS, i++)) && strcmp((const char *)extension, "GL_ARB_timer_query"));
	if (!extension) {
		fprintf(stderr, PREFIX "Error: extension GL_ARB_timer_query is not available\n");
		return -1;
	}

	INIT_GLEXT_AND_RETURN_ON_NULL(PFNGLGETQUERYIVPROC, glGetQueryiv);
	INIT_GLEXT_AND_RETURN_ON_NULL(PFNGLGENQUERIESPROC, glGenQueries);
	INIT_GLEXT_AND_RETURN_ON_NULL(PFNGLQUERYCOUNTERPROC, glQueryCounter);
	INIT_GLEXT_AND_RETURN_ON_NULL(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv);
	INIT_GLEXT_AND_RETURN_ON_NULL(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v);

#	undef INIT_GLEXT_AND_RETURN_ON_NULL

	glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &timerqueries_numbits);
	if (timerqueries_debug)
		printf(PREFIX "Number of bits of timer query counters: %d\n", timerqueries_numbits);
	if (timerqueries_numbits < 30) {
		// According to the spec, implementations with less than 30 bits (frame of ~ 1s) are not allowed.
		fprintf(stderr, PREFIX "Error: number of bits of timer query counters (%d) is too small\n", timerqueries_numbits);
		return -1;
	}

	glGenQueries(TIMERQUERIES_NUM, timerqueries);
	return 0;
}

static void timing_timerqueries() {

	if (!timerqueries_idxinit) {
		// Initialize timer queries now, because we now have a valid OpenGL context.
		if (init_timerqueries()) {
			// Degrade gracefully upon failure by using timing_cpuclock() instead.
			fprintf(stderr, PREFIX "Failed to init timer queries, therefore using cpu clock instead\n");
			timerqueries_disabled = 1;
			return;
		}
	}

	// Schedule a record of the GPU timestamp after the GPU commands submitted for this frame (i.e. at idx back).
	glQueryCounter(timerqueries[timerqueries_idxback], GL_TIMESTAMP);

	if (timerqueries_idxinit < TIMERQUERIES_NUM - 1) {
		// Don't query any result yet, since not all timer queries are scheduled yet.
		++timerqueries_idxinit;
	} else {
		if (timerqueries_debug) {
			// Verify assumption "result is available"; if not true, querying the result would stall the pipeline.
			GLint available = 0;
			glGetQueryObjectiv(timerqueries[timerqueries_idxfront], GL_QUERY_RESULT_AVAILABLE, &available);
			if (!available)
				fprintf(stderr, PREFIX "Warning: timer query result not yet available, "
						"TIMERQUERIES_NUM (%d) is too low => please report this issue!\n", TIMERQUERIES_NUM);
		}

		// Query the oldest result (i.e. at idx front), we assume the result is available by now.
		GLuint64 timerqueries_newtime = 0;
		glGetQueryObjectui64v(timerqueries[timerqueries_idxfront], GL_QUERY_RESULT, &timerqueries_newtime);

		if (timerqueries_idxinit < TIMERQUERIES_NUM) {
			// timerqueries_oldtime is not yet valid, so we can't calculate a delta yet.
			timerqueries_idxinit = TIMERQUERIES_NUM;
		} else {
			// Calculate delta, consider timer query bits, and perform integer rounding to convert from ns to us.
			const GLuint64 nsec = (timerqueries_newtime - timerqueries_oldtime) &
					(timerqueries_numbits < 64 ?
						((GLuint64) 1 << timerqueries_numbits) - 1 : (GLuint64) -1);
			const GLuint64 usec = (nsec + 500) / 1000;

			fprintf(f, "Frametime %" PRIu64 " us\n", usec);
			fflush(f);
		}
		timerqueries_oldtime = timerqueries_newtime;
	}

	// 'Swap'/rotate idx pointers to timer queries.
	timerqueries_idxback = timerqueries_idxfront;
	timerqueries_idxfront = (timerqueries_idxfront + 1) % TIMERQUERIES_NUM;
}
#endif

static void timing() {

#	ifdef GL_ARB_timer_query
	if (!timerqueries_disabled)
		timing_timerqueries();
#	endif

	if (timerqueries_disabled)
		timing_cpuclock();
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

static void *load_func_from_next(const char * const function_name) {

	dlerror();
	void * const real_function = real_dlsym(RTLD_NEXT, function_name);

	const char * const err = dlerror();
	if (err)
		die(PREFIX "dlsym failed: %s\n", err);

	return real_function;
}

static void *load_func_from_lib(const char * const function_name, const char * const library_name) {

	// If possible, load from the next object such that we don't have to load the lib.
	void *real_function = load_func_from_next(function_name);

	if (!real_function) {
		void * const lib = dlopen(library_name, RTLD_LAZY);
		if (!lib)
			die(PREFIX "Failed to open %s\n", library_name);

		real_function = real_dlsym(lib, function_name);
		if (!real_function)
			die(PREFIX "Failed to find %s in %s\n", function_name, library_name);
	}

	return real_function;
}

static void init() __attribute__((constructor));
static void deinit() __attribute__((destructor));

static void init() {

	const char *env = getenv("LIBFRAMETIME_FILE");
	const char * const name = env ? env : "/tmp/libframetime.out";

	f = fopen(name, "w");

	if (!f)
		die(PREFIX "Failed to open %s for writing\n", name);

#	ifdef GL_ARB_timer_query
	env = getenv("LIBFRAMETIME_TIMERQUERIES_DISABLED");
	timerqueries_disabled = (env && !strcmp(env, "1"));

	env = getenv("LIBFRAMETIME_TIMERQUERIES_DEBUG");
	timerqueries_debug = (env && !strcmp(env, "1"));
#	endif

	if (!real_dlsym)
		init_dlsym();

	real_glXGetProcAddressARB = load_func_from_lib("glXGetProcAddressARB", "libGL.so");
	real_glXSwapBuffers = load_func_from_lib("glXSwapBuffers", "libGL.so");
#	ifndef NO_EGL
	real_eglSwapBuffers = load_func_from_lib("eglSwapBuffers", "libEGL.so");
#	endif
}

static void deinit() {

	fflush(f);
	fsync(fileno(f));
	fclose(f);
}
