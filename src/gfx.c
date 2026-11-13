#include "gfx.h"

#include <GLES2/gl2.h>
#include <stdlib.h>

#include "log.h"

static const char *vs_src =
	"attribute vec2 a_pos;\n"
	"attribute vec2 a_uv;\n"
	"uniform vec2 u_screen;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"  v_uv = a_uv;\n"
	"  gl_Position = vec4(a_pos.x / u_screen.x * 2.0 - 1.0,\n"
	"                     1.0 - a_pos.y / u_screen.y * 2.0, 0.0, 1.0);\n"
	"}\n";

/* Textured quads. u_sharp: sharp bilinear, with u_prescale the integer
 * part of the scale; the texture is sampled linear and the bilinear step
 * is squeezed into the last fraction of each texel. */
static const char *fs_tex_src =
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform sampler2D u_tex;\n"
	"uniform vec2 u_tex_size;\n"
	"uniform vec2 u_prescale;\n"
	"uniform float u_sharp;\n"
	"uniform float u_bgr;\n"
	"uniform float u_alpha;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"  vec2 uv = v_uv;\n"
	"  if (u_sharp > 0.5) {\n"
	"    vec2 texel = uv * u_tex_size;\n"
	"    vec2 base = floor(texel);\n"
	"    vec2 d = fract(texel) - 0.5;\n"
	"    vec2 range = 0.5 - 0.5 / u_prescale;\n"
	"    vec2 f = (d - clamp(d, -range, range)) * u_prescale + 0.5;\n"
	"    uv = (base + f) / u_tex_size;\n"
	"  }\n"
	"  vec4 c = texture2D(u_tex, uv);\n"
	"  if (u_bgr > 0.5) c = c.bgra;\n"
	"  gl_FragColor = vec4(c.rgb, u_alpha);\n"
	"}\n";

static const char *fs_fill_src =
	"precision mediump float;\n"
	"uniform vec4 u_color;\n"
	"void main() { gl_FragColor = u_color; }\n";

static const char *fs_text_src =
	"precision mediump float;\n"
	"uniform sampler2D u_tex;\n"
	"uniform vec4 u_color;\n"
	"varying vec2 v_uv;\n"
	"void main() { gl_FragColor = vec4(u_color.rgb, u_color.a * texture2D(u_tex, v_uv).a); }\n";

struct prog {
	GLuint id;
	GLint screen, tex, tex_size, prescale, sharp, bgr, alpha, color;
};

static struct prog p_tex, p_fill, p_text;
static struct tex atlas;
static int scr_w, scr_h;

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char msg[512];
		glGetShaderInfoLog(s, sizeof msg, NULL, msg);
		LOG("shader: %s", msg);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static int link(struct prog *p, const char *fs)
{
	GLuint v = compile(GL_VERTEX_SHADER, vs_src), f = compile(GL_FRAGMENT_SHADER, fs);
	if (!v || !f)
		return -1;
	p->id = glCreateProgram();
	glAttachShader(p->id, v);
	glAttachShader(p->id, f);
	glBindAttribLocation(p->id, 0, "a_pos");
	glBindAttribLocation(p->id, 1, "a_uv");
	glLinkProgram(p->id);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint ok = 0;
	glGetProgramiv(p->id, GL_LINK_STATUS, &ok);
	if (!ok) {
		char msg[512];
		glGetProgramInfoLog(p->id, sizeof msg, NULL, msg);
		LOG("program: %s", msg);
		return -1;
	}
	p->screen = glGetUniformLocation(p->id, "u_screen");
	p->tex = glGetUniformLocation(p->id, "u_tex");
	p->tex_size = glGetUniformLocation(p->id, "u_tex_size");
	p->prescale = glGetUniformLocation(p->id, "u_prescale");
	p->sharp = glGetUniformLocation(p->id, "u_sharp");
	p->bgr = glGetUniformLocation(p->id, "u_bgr");
	p->alpha = glGetUniformLocation(p->id, "u_alpha");
	p->color = glGetUniformLocation(p->id, "u_color");
	glUseProgram(p->id);
	glUniform2f(p->screen, (float)scr_w, (float)scr_h);
	if (p->tex >= 0)
		glUniform1i(p->tex, 0);
	return 0;
}

int gfx_init(int screen_w, int screen_h)
{
	scr_w = screen_w;
	scr_h = screen_h;
	if (link(&p_tex, fs_tex_src) || link(&p_fill, fs_fill_src) || link(&p_text, fs_text_src))
		return -1;
	glViewport(0, 0, screen_w, screen_h);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	int aw, ah;
	const uint8_t *px = font_atlas_pixels(&aw, &ah);
	if (tex_create(&atlas, aw, ah, TEX_ALPHA))
		return -1;
	tex_upload(&atlas, px, aw, ah);
	return 0;
}

void gfx_shutdown(void)
{
	tex_destroy(&atlas);
	glDeleteProgram(p_tex.id);
	glDeleteProgram(p_fill.id);
	glDeleteProgram(p_text.id);
}

static void set_color(GLint loc, rgba c)
{
	glUniform4f(loc, (float)(c >> 24 & 255) / 255.f, (float)(c >> 16 & 255) / 255.f,
	            (float)(c >> 8 & 255) / 255.f, (float)(c & 255) / 255.f);
}

void gfx_clear(rgba c)
{
	glClearColor((float)(c >> 24 & 255) / 255.f, (float)(c >> 16 & 255) / 255.f,
	             (float)(c >> 8 & 255) / 255.f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void gl_format(enum tex_format f, GLenum *fmt, GLenum *type, int *bpp)
{
	switch (f) {
	case TEX_RGB565:
		*fmt = GL_RGB, *type = GL_UNSIGNED_SHORT_5_6_5, *bpp = 2;
		break;
	case TEX_XRGB8888:
		*fmt = GL_RGBA, *type = GL_UNSIGNED_BYTE, *bpp = 4;
		break;
	case TEX_RGB888:
		*fmt = GL_RGB, *type = GL_UNSIGNED_BYTE, *bpp = 3;
		break;
	case TEX_ALPHA:
	default:
		*fmt = GL_ALPHA, *type = GL_UNSIGNED_BYTE, *bpp = 1;
		break;
	}
}

int tex_create(struct tex *t, int w, int h, enum tex_format fmt)
{
	GLenum gf, gt;
	int bpp;
	gl_format(fmt, &gf, &gt, &bpp);
	glGenTextures(1, &t->id);
	glBindTexture(GL_TEXTURE_2D, t->id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, (GLint)gf, w, h, 0, gf, gt, NULL);
	t->w = w;
	t->h = h;
	t->fmt = fmt;
	return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void tex_upload(struct tex *t, const void *pixels, int w, int h)
{
	if (w > t->w || h > t->h) {
		enum tex_format f = t->fmt;
		tex_destroy(t);
		tex_create(t, w > t->w ? w : t->w, h > t->h ? h : t->h, f);
	}
	GLenum gf, gt;
	int bpp;
	gl_format(t->fmt, &gf, &gt, &bpp);
	glBindTexture(GL_TEXTURE_2D, t->id);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, gf, gt, pixels);
}

void tex_destroy(struct tex *t)
{
	if (t->id)
		glDeleteTextures(1, &t->id);
	t->id = 0;
}

static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
	const GLfloat pos[] = {x0, y0, x1, y0, x0, y1, x1, y1};
	const GLfloat uv[] = {u0, v0, u1, v0, u0, v1, u1, v1};
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gfx_draw(const struct tex *t, struct rect src, struct rect dst, enum tex_filter filter, float alpha)
{
	if (!t->id || src.w <= 0 || src.h <= 0)
		return;
	glUseProgram(p_tex.id);
	glBindTexture(GL_TEXTURE_2D, t->id);
	GLint f = filter == FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
	glUniform2f(p_tex.tex_size, (float)t->w, (float)t->h);
	float px = (float)(dst.w / src.w), py = (float)(dst.h / src.h);
	glUniform2f(p_tex.prescale, px < 1 ? 1 : px, py < 1 ? 1 : py);
	glUniform1f(p_tex.sharp, filter == FILTER_SHARP ? 1.f : 0.f);
	glUniform1f(p_tex.bgr, t->fmt == TEX_XRGB8888 ? 1.f : 0.f);
	glUniform1f(p_tex.alpha, alpha);
	if (alpha < 1.f)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
	float tw = (float)t->w, th = (float)t->h;
	quad((float)dst.x, (float)dst.y, (float)(dst.x + dst.w), (float)(dst.y + dst.h),
	     (float)src.x / tw, (float)src.y / th, (float)(src.x + src.w) / tw, (float)(src.y + src.h) / th);
}

void gfx_fill(struct rect r, rgba color)
{
	glUseProgram(p_fill.id);
	set_color(p_fill.color, color);
	glEnable(GL_BLEND);
	quad((float)r.x, (float)r.y, (float)(r.x + r.w), (float)(r.y + r.h), 0, 0, 0, 0);
}

float gfx_text(enum font_face face, float x, float y, rgba color, const char *utf8)
{
	glUseProgram(p_text.id);
	set_color(p_text.color, color);
	glBindTexture(GL_TEXTURE_2D, atlas.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glEnable(GL_BLEND);
	float start = x, aw = (float)atlas.w, ah = (float)atlas.h;
	while (*utf8) {
		const struct glyph *g = font_glyph(face, utf8_next(&utf8));
		if (g->w > 0 && g->h > 0) {
			/* Whole pixels, so the atlas maps 1:1 and stays crisp. */
			float gx = (float)(int)(x + 0.5f) + (float)g->ox, gy = y + (float)g->oy;
			quad(gx, gy, gx + (float)g->w, gy + (float)g->h, (float)g->x / aw, (float)g->y / ah,
			     (float)(g->x + g->w) / aw, (float)(g->y + g->h) / ah);
		}
		x += g->adv;
	}
	return x - start;
}
