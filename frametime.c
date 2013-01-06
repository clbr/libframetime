#define _GNU_SOURCE

#include <stdio.h>
#include <dlfcn.h>
#include <GL/glx.h>

void (*realswap)(Display *dpy, GLXDrawable drawable) = NULL;

void glXSwapBuffers(Display *dpy, GLXDrawable drawable) {
	puts("Swappan");
}

static void init() __attribute__((constructor));
static void init() {
	puts("Buildan");
}
