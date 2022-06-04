#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

#include <grub/multiboot2.h>
#include <stddef.h>
#include <lib/font.h>

#define TEXT_BLACK			0
#define TEXT_LIGHT_GREY			7
#define TEXT_COLOR(bg, fg)		(((bg) << 4) | (fg))
#define TEXT_ENTRY(bg, fg, c)		((TEXT_COLOR((bg), (fg)) << 8) | (c))

/*
 * Frame buffer structure.
 */
struct framebuffer_t {
	uint32_t		addr;
	uint16_t		type;
	uint32_t		pitch;
	uint32_t		width;
	uint32_t		height;
	uint8_t			bpp;
	struct font_t *		font;
	uint32_t		x;
	uint32_t		y;
	uint8_t			red;
	uint8_t			green;
	uint8_t			blue;
	char *			buf;
	char			dirty;
	void			(*update)(struct framebuffer_t *);
};

int init_framebuffer(struct framebuffer_t *fb, struct multiboot_tag_framebuffer *tag_fb);
void fb_set_xy(struct framebuffer_t *fb, uint32_t x, uint32_t y);
void fb_putc(struct framebuffer_t *fb, uint8_t c);

/*
 * Delete a character at a position.
 */
static inline void fb_del(struct framebuffer_t *fb, uint32_t x, uint32_t y)
{
	fb->buf[y * fb->width + x] = ' ';
}

#endif
