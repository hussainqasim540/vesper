#include "display.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "log.h"

struct priv {
	uint32_t conn_id, crtc_id;
	drmModeModeInfo mode;
	drmModeCrtc *saved_crtc;
	struct gbm_device *gbm;
	struct gbm_surface *surf;
	EGLDisplay dpy;
	EGLContext ctx;
	EGLSurface esurf;
	struct gbm_bo *front;
	int mode_set;
	int offscreen;
};

static void fb_destroy(struct gbm_bo *bo, void *data)
{
	int fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	uint32_t fb = (uint32_t)(uintptr_t)data;
	if (fb)
		drmModeRmFB(fd, fb);
}

static uint32_t fb_for(int fd, struct gbm_bo *bo)
{
	uint32_t fb = (uint32_t)(uintptr_t)gbm_bo_get_user_data(bo);
	if (fb)
		return fb;
	uint32_t handles[4] = {gbm_bo_get_handle(bo).u32}, pitches[4] = {gbm_bo_get_stride(bo)}, offsets[4] = {0};
	if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), GBM_FORMAT_XRGB8888,
	                  handles, pitches, offsets, &fb, 0)) {
		LOG("drmModeAddFB2: %s", strerror(errno));
		return 0;
	}
	gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb, fb_destroy);
	return fb;
}

/* The first card with a connected connector: on the R36S the rockchip
 * DRM device and its DSI panel. */
static int find_output(struct display *d, struct priv *p)
{
	char path[32];
	for (int i = 0; i < 4; i++) {
		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmModeRes *res = drmModeGetResources(fd);
		if (!res) {
			close(fd);
			continue;
		}
		for (int c = 0; c < res->count_connectors; c++) {
			drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[c]);
			if (!conn)
				continue;
			if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
				drmModeFreeConnector(conn);
				continue;
			}
			p->mode = conn->modes[0];
			for (int m = 0; m < conn->count_modes; m++)
				if (conn->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
					p->mode = conn->modes[m];
					break;
				}
			p->conn_id = conn->connector_id;
			/* The CRTC of the current encoder, or any it can drive. */
			p->crtc_id = 0;
			drmModeEncoder *enc = conn->encoder_id ? drmModeGetEncoder(fd, conn->encoder_id) : NULL;
			if (enc && enc->crtc_id)
				p->crtc_id = enc->crtc_id;
			if (enc)
				drmModeFreeEncoder(enc);
			for (int e = 0; !p->crtc_id && e < conn->count_encoders; e++) {
				enc = drmModeGetEncoder(fd, conn->encoders[e]);
				if (!enc)
					continue;
				for (int k = 0; k < res->count_crtcs; k++)
					if (enc->possible_crtcs & (1u << k)) {
						p->crtc_id = res->crtcs[k];
						break;
					}
				drmModeFreeEncoder(enc);
			}
			drmModeFreeConnector(conn);
			if (p->crtc_id) {
				drmModeFreeResources(res);
				d->fd = fd;
				LOG("display %s: %ux%u@%u", path, p->mode.hdisplay, p->mode.vdisplay, p->mode.vrefresh);
				return 0;
			}
		}
		drmModeFreeResources(res);
		close(fd);
	}
	LOG("no connected display");
	return -1;
}

static int init_egl(struct priv *p)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	p->dpy = get_platform_display ? get_platform_display(EGL_PLATFORM_GBM_KHR, p->gbm, NULL)
	                              : eglGetDisplay((EGLNativeDisplayType)p->gbm);
	EGLint major, minor;
	if (p->dpy == EGL_NO_DISPLAY || !eglInitialize(p->dpy, &major, &minor)) {
		LOG("eglInitialize failed");
		return -1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint attrs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE,
	};
	EGLint n = 0;
	eglChooseConfig(p->dpy, attrs, NULL, 0, &n);
	if (n <= 0) {
		LOG("no EGL config");
		return -1;
	}
	EGLConfig *configs = calloc((size_t)n, sizeof *configs), config = NULL;
	eglChooseConfig(p->dpy, attrs, configs, n, &n);
	for (int i = 0; i < n; i++) {
		EGLint id;
		if (eglGetConfigAttrib(p->dpy, configs[i], EGL_NATIVE_VISUAL_ID, &id) && id == GBM_FORMAT_XRGB8888) {
			config = configs[i];
			break;
		}
	}
	free(configs);
	if (!config) {
		LOG("no EGL config for XRGB8888");
		return -1;
	}
	const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	p->ctx = eglCreateContext(p->dpy, config, EGL_NO_CONTEXT, ctx_attrs);
	p->esurf = eglCreateWindowSurface(p->dpy, config, (EGLNativeWindowType)p->surf, NULL);
	if (p->ctx == EGL_NO_CONTEXT || p->esurf == EGL_NO_SURFACE ||
	    !eglMakeCurrent(p->dpy, p->esurf, p->esurf, p->ctx)) {
		LOG("EGL context or surface failed: 0x%x", eglGetError());
		return -1;
	}
	LOG("EGL %d.%d, %s", major, minor, eglQueryString(p->dpy, EGL_VENDOR));
	return 0;
}

int display_open(struct display *d)
{
	struct priv *p = calloc(1, sizeof *p);
	if (!p)
		return -1;
	d->priv = p;
	d->fd = -1;
	if (find_output(d, p))
		return -1;
	d->w = p->mode.hdisplay;
	d->h = p->mode.vdisplay;
	d->refresh_mhz = (int)((long long)p->mode.clock * 1000000LL / ((long long)p->mode.htotal * p->mode.vtotal));
	p->saved_crtc = drmModeGetCrtc(d->fd, p->crtc_id);
	p->gbm = gbm_create_device(d->fd);
	if (!p->gbm)
		return -1;
	p->surf = gbm_surface_create(p->gbm, (uint32_t)d->w, (uint32_t)d->h, GBM_FORMAT_XRGB8888,
	                             GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!p->surf) {
		LOG("gbm_surface_create failed");
		return -1;
	}
	return init_egl(p);
}

static void flip_done(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
	(void)fd, (void)seq, (void)sec, (void)usec;
	*(int *)data = 0;
}

int display_open_offscreen(struct display *d, int w, int h)
{
	struct priv *p = calloc(1, sizeof *p);
	if (!p)
		return -1;
	d->priv = p;
	d->fd = -1;
	d->w = w;
	d->h = h;
	d->refresh_mhz = 60000;
	p->offscreen = 1;
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (!get_platform_display)
		return -1;
	p->dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (p->dpy == EGL_NO_DISPLAY || !eglInitialize(p->dpy, NULL, NULL)) {
		LOG("surfaceless EGL failed");
		return -1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint attrs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE,
	};
	EGLConfig config;
	EGLint n = 0;
	if (!eglChooseConfig(p->dpy, attrs, &config, 1, &n) || n < 1) {
		LOG("no pbuffer config");
		return -1;
	}
	const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	const EGLint pb_attrs[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
	p->ctx = eglCreateContext(p->dpy, config, EGL_NO_CONTEXT, ctx_attrs);
	p->esurf = eglCreatePbufferSurface(p->dpy, config, pb_attrs);
	if (p->ctx == EGL_NO_CONTEXT || p->esurf == EGL_NO_SURFACE ||
	    !eglMakeCurrent(p->dpy, p->esurf, p->esurf, p->ctx)) {
		LOG("pbuffer context failed: 0x%x", eglGetError());
		return -1;
	}
	return 0;
}

int display_read_rgb(struct display *d, unsigned char *out)
{
	unsigned char *rgba = malloc((size_t)d->w * d->h * 4);
	if (!rgba)
		return -1;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, d->w, d->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	for (int y = 0; y < d->h; y++) /* GL's rows start at the bottom */
		for (int x = 0; x < d->w; x++)
			for (int c = 0; c < 3; c++)
				out[((size_t)y * d->w + x) * 3 + c] = rgba[((size_t)(d->h - 1 - y) * d->w + x) * 4 + c];
	free(rgba);
	return 0;
}

int display_present(struct display *d)
{
	struct priv *p = d->priv;
	if (p->offscreen) {
		glFinish();
		return 0;
	}
	eglSwapBuffers(p->dpy, p->esurf);
	struct gbm_bo *next = gbm_surface_lock_front_buffer(p->surf);
	if (!next)
		return -1;
	uint32_t fb = fb_for(d->fd, next);
	if (!fb) {
		gbm_surface_release_buffer(p->surf, next);
		return -1;
	}
	if (!p->mode_set) {
		if (drmModeSetCrtc(d->fd, p->crtc_id, fb, 0, 0, &p->conn_id, 1, &p->mode)) {
			LOG("drmModeSetCrtc: %s", strerror(errno));
			gbm_surface_release_buffer(p->surf, next);
			return -1;
		}
		p->mode_set = 1;
	} else {
		int waiting = 1;
		if (drmModePageFlip(d->fd, p->crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT, &waiting)) {
			LOG("drmModePageFlip: %s", strerror(errno));
			gbm_surface_release_buffer(p->surf, next);
			return -1;
		}
		drmEventContext ev = {.version = 2, .page_flip_handler = flip_done};
		while (waiting) {
			struct pollfd pfd = {.fd = d->fd, .events = POLLIN};
			int r = poll(&pfd, 1, 100);
			if (r < 0 && errno == EINTR)
				continue;
			if (r <= 0) {
				LOG("page flip timed out");
				break;
			}
			drmHandleEvent(d->fd, &ev);
		}
	}
	if (p->front)
		gbm_surface_release_buffer(p->surf, p->front);
	p->front = next;
	return 0;
}

void display_close(struct display *d)
{
	struct priv *p = d->priv;
	if (!p)
		return;
	if (p->saved_crtc) {
		drmModeCrtc *c = p->saved_crtc;
		if (c->buffer_id)
			drmModeSetCrtc(d->fd, c->crtc_id, c->buffer_id, c->x, c->y, &p->conn_id, 1, &c->mode);
		drmModeFreeCrtc(c);
	}
	if (p->dpy != EGL_NO_DISPLAY && p->dpy) {
		eglMakeCurrent(p->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (p->esurf)
			eglDestroySurface(p->dpy, p->esurf);
		if (p->ctx)
			eglDestroyContext(p->dpy, p->ctx);
		eglTerminate(p->dpy);
	}
	if (p->front)
		gbm_surface_release_buffer(p->surf, p->front);
	if (p->surf)
		gbm_surface_destroy(p->surf);
	if (p->gbm)
		gbm_device_destroy(p->gbm);
	if (d->fd >= 0)
		close(d->fd);
	free(p);
	d->priv = NULL;
}
