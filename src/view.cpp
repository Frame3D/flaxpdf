/*
Copyright (C) 2015 Lauri Kasanen

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "view.h"
#include <TextOutputDev.h>
#include <glib/poppler-features.h>

// Quarter inch in double resolution
#define MARGIN 36

pdfview::pdfview(int x, int y, int w, int h): Fl_Widget(x, y, w, h),
		yoff(0), xoff(0),
		selx(0), sely(0), selx2(0), sely2(0) {

	cachedsize = 7 * 1024 * 1024;

	u32 i;
	for (i = 0; i < CACHE_MAX; i++) {
		cache[i] = (u8 *) xcalloc(cachedsize, 1);
		cachedpage[i] = USHRT_MAX;
		pix[i] = None;
	}
}

void pdfview::resetselection() {
	selx = sely = selx2 = sely2 = 0;
}

void pdfview::reset() {
	yoff = 0;
	xoff = 0;

	resetselection();

	u32 i;
	for (i = 0; i < CACHE_MAX; i++) {
		cachedpage[i] = USHRT_MAX;
	}
}

static u32 fullh(u32 page) {
	if (!file->cache[page].ready)
		page = 0;

	if (file->mode == Z_TRIM)
		return file->cache[page].h;

	return file->cache[page].h +
		file->cache[page].top +
		file->cache[page].bottom;
}

static u32 fullw(u32 page) {
	if (!file->cache[page].ready)
		page = 0;

	if (file->mode == Z_TRIM)
		return file->cache[page].w;

	return file->cache[page].w +
		file->cache[page].left +
		file->cache[page].right;
}

static bool hasmargins(const u32 page) {
	if (!file->cache[page].ready)
		return file->cache[0].left > MARGIN ||
			file->cache[0].right > MARGIN ||
			file->cache[0].top > MARGIN ||
			file->cache[0].bottom > MARGIN;

	return file->cache[page].left > MARGIN ||
		file->cache[page].right > MARGIN ||
		file->cache[page].top > MARGIN ||
		file->cache[page].bottom > MARGIN;
}

static void updatevisible(const float yoff, const u32 w, const u32 h, const bool fromdraw) {
	// From the current zoom mode and view offset, update the visible page info
	const u32 prev = file->first_visible;
	file->first_visible = yoff < 0 ? 0 : yoff;
	if (file->first_visible > file->pages - 1)
		file->first_visible = file->pages - 1;
	u32 i;

	const u32 maxw = file->maxw ? file->maxw : file->cache[0].w;
	const u32 maxh = file->maxh ? file->maxh : file->cache[0].h;
	const u32 maxwmargin = hasmargins(file->first_visible) ? maxw + MARGIN * 2 : maxw;
	const u32 fullw = ::fullw(0);
	const u32 fullh = ::fullh(0);

	const float visible = yoff < 0 ? 0 : 1 - (yoff - floorf(yoff));

	u32 usedh = fullh;
	switch (file->mode) {
		case Z_TRIM:
			file->zoom = w / (float) maxwmargin;
			usedh = maxh;
		break;
		case Z_WIDTH:
			file->zoom = w / (float) fullw;
		break;
		case Z_PAGE:
			if (fullw > fullh) {
				file->zoom = w / (float) fullw;
			} else {
				file->zoom = h / (float) fullh;
			}
		break;
		case Z_CUSTOM:
		break;
	}

	const u32 zoomedmargin = MARGIN * file->zoom;
	i = file->first_visible;
	u32 tmp = visible * usedh * file->zoom;
	tmp += zoomedmargin;
	while (tmp < h) {
		tmp += usedh * file->zoom;
		tmp += zoomedmargin;
		i++;
	}
	// Be conservative
	if (i < file->pages - 1)
		i++;
	if (i > file->pages - 1)
		i = file->pages - 1;
	file->last_visible = i;

	if (prev != file->first_visible) {
		char buf[10];
		snprintf(buf, 10, "%u", file->first_visible + 1);
		pagebox->value(buf);

		if (!fromdraw)
			pagebox->redraw();
	}
}

void pdfview::draw() {

	if (!file->cache)
		return;

	updatevisible(yoff, w(), h(), true);

	const Fl_Color pagecol = FL_WHITE;
	int X, Y, W, H;
	fl_clip_box(x(), y(), w(), h(), X, Y, W, H);
	if (!W)
		return;

	fl_rectf(X, Y, W, H, FL_GRAY + 1);

	struct cachedpage *cur = &file->cache[file->first_visible];
	if (!cur->ready)
		return;

	fl_push_clip(X, Y, W, H);

	// As the variables are used for calculations, reset them to full widget dimensions.
	X = x();
	Y = y();
	W = w();
	H = h();

	// Fill each page rect
	const int zoomedmargin = file->zoom * MARGIN;
	const int zoomedmarginhalf = zoomedmargin / 2;
	u32 i;
	const u32 max = file->last_visible;
	const float visible = yoff - floorf(yoff);
	H = pxrel(file->first_visible);
	Y = y() - visible * H;

	for (i = file->first_visible; i <= max; i++) {
		cur = &file->cache[i];
		if (!cur->ready)
			break;

		H = (fullh(i) + MARGIN) * file->zoom;
		if (file->mode == Z_CUSTOM || file->mode == Z_PAGE) {
			W = fullw(i) * file->zoom;
			X = x() + (w() - W) / 2 + xoff * W;
		} else {
			X = x();
			W = w();

			// In case of different page sizes, H needs to be adjusted per-page
			const float ratio = W / (float) fullw(i);
			H = fullh(i) * ratio + zoomedmargin;
		}

		// XYWH is now the full area including grey margins.

		Y += zoomedmarginhalf;
		H -= zoomedmargin;

		// XYWH is now the page's area.
		if (Y >= y() + h())
			break;
		fl_rectf(X, Y, W, H, pagecol);

		const bool margins = hasmargins(i);
		const bool trimmed = margins && file->mode == Z_TRIM;
		if (trimmed) {
			// If the page was trimmed, have the real one a bit smaller
			X += zoomedmarginhalf;
			Y += zoomedmarginhalf;
			W -= zoomedmargin;
			H -= zoomedmargin;
		} else if (margins) {
			// Restore the full size with empty borders
			X += cur->left * file->zoom;
			Y += cur->top * file->zoom;
			W -= (cur->left + cur->right) * file->zoom;
			H -= (cur->top + cur->bottom) * file->zoom;
		}

		// Render real content
		content(i, X, Y, W, H);

		if (trimmed) {
			// And undo.
			Y -= zoomedmarginhalf;
			H += zoomedmargin;
		} else if (margins) {
			// And undo.
			Y -= cur->top * file->zoom;
			H += (cur->top + cur->bottom) * file->zoom;
		}

		Y -= zoomedmarginhalf;
		H += zoomedmargin;
		Y += H;
	}

	fl_pop_clip();
}

u32 pdfview::pxrel(u32 page) const {
	if (file->mode != Z_CUSTOM && file->mode != Z_PAGE) {
		const float ratio = w() / (float) fullw(page);
		return fullh(page) * ratio + MARGIN * file->zoom;
	}
	return (fullh(page) + MARGIN) * file->zoom;
}

float pdfview::maxyoff() const {

	const u32 last = file->pages - 1;
	if (!file->cache[last].ready)
		return last + 0.5f;

	s32 sh = pxrel(last);

	const s32 hidden = sh - h();

	float f = last;

	if (hidden > 0) {
		f += hidden / (float) sh;
	} else {
		f -= 1 - (sh / (float) h());
		f += MARGIN / (float) h();
	}

	if (f < 0)
		return 0;

	return f;
}

int pdfview::handle(int e) {

	if (!file->cache)
		return Fl_Widget::handle(e);

	const float move = 0.05f;
	static int lasty, lastx;

	switch (e) {
		case FL_RELEASE:
			// Was this a dragging text selection?
			if (selecting->value() && Fl::event_button() == FL_LEFT_MOUSE &&
				selx && sely && selx2 && sely2 && selx != selx2 &&
				sely != sely2) {

				const u32 zoomedmargin = MARGIN * file->zoom;
				const u32 zoomedmarginhalf = zoomedmargin / 2;

				// Which page is selx,sely on?
				u32 page = yoff;
				const float floored = yoff - floorf(yoff);
				const float visible = 1 - floored;
				u32 zoomedh = fullh(page) * visible * file->zoom;
				const u32 ratiominus = hasmargins(page) ? zoomedmargin : 0;
				const float ratio = w() / (float) fullw(page);
				const float ratiox = (w() - ratiominus) / (float) fullw(page);
				if (file->mode != Z_CUSTOM && file->mode != Z_PAGE) {
					zoomedh = fullh(page) * visible * ratio;
				}

				if (y() + zoomedh < sely)
					page++;

				// We assume nobody selects text with a tiny zoom.
				u32 X, Y, W, H;
				if (selx < selx2) {
					X = selx;
					W = selx2 - X;
				} else {
					X = selx2;
					W = selx - X;
				}

				if (sely < sely2) {
					Y = sely;
					H = sely2 - Y;
				} else {
					Y = sely2;
					H = sely - Y;
				}

				// Convert to widget coords
				X -= x();
				Y -= y();

				// Offset
				switch (file->mode) {
					case Z_TRIM:
						// X and Y start in widget space, 0 to w/h.
						Y += floored * (fullh(page) * ratiox +
								zoomedmargin + ratiominus);
						Y -= zoomedmarginhalf; // Grey margin

						if (hasmargins(page)) {
							X -= zoomedmarginhalf;
							Y -= zoomedmarginhalf;
						}

						// Now page's area.

						X += file->cache[page].left * ratiox;
						Y += file->cache[page].top * ratiox;

						X /= ratiox;
						Y /= ratiox;
						W /= ratiox;
						H /= ratiox;
					break;
					case Z_WIDTH:
						// X and Y start in widget space, 0 to w/h.
						Y += floored * (fullh(page) * ratio +
								zoomedmargin);
						Y -= zoomedmarginhalf; // Grey margin

						X /= ratio;
						Y /= ratio;
						W /= ratio;
						H /= ratio;
					break;
					case Z_PAGE:
					case Z_CUSTOM:
						// X and Y start in widget space, 0 to w/h.
						Y += floored * (fullh(page) * file->zoom +
								zoomedmargin);
						Y -= zoomedmarginhalf; // Grey margin

						X -= (w() - (fullw(page) * file->zoom)) / 2 +
							xoff * (fullw(page) * file->zoom);

						X /= file->zoom;
						Y /= file->zoom;
						W /= file->zoom;
						H /= file->zoom;
					break;
				}

				TextOutputDev * const dev = new TextOutputDev(NULL, true,
								0, false, false);
				file->pdf->displayPage(dev, page + 1, 144, 144, 0,
								true, false, false);
				GooString *str = dev->getText(X, Y, X + W, Y + H);
				const char * const cstr =
#if POPPLER_CHECK_VERSION(0, 72, 0)
					str->c_str();
#else
					str->getCString();
#endif

				// Put it to clipboard
				Fl::copy(cstr, strlen(cstr));

				delete str;
				delete dev;
			}
		break;
		case FL_PUSH:
			take_focus();
			lasty = Fl::event_y();
			lastx = Fl::event_x();

			resetselection();
			if (selecting->value() && Fl::event_button() == FL_LEFT_MOUSE) {
				selx = lastx;
				sely = lasty;
			}
			redraw();

			// Fall-through
		case FL_FOCUS:
		case FL_ENTER:
			return 1;
		break;
		case FL_DRAG: {
			const int my = Fl::event_y();
			const int mx = Fl::event_x();
			const int movedy = my - lasty;
			const int movedx = mx - lastx;

			if (selecting->value() && Fl::event_button() == FL_LEFT_MOUSE) {
				selx2 = mx;
				sely2 = my;
				redraw();
				return 1;
			}

			fl_cursor(FL_CURSOR_MOVE);

			if (file->maxh) {
				if (file->mode != Z_TRIM)
					yoff -= (movedy / file->zoom) / fullh(0);
				else
					yoff -= (movedy / file->zoom) / file->maxh;
			} else {
				yoff -= (movedy / (float) h()) / file->zoom;
			}

			xoff += (movedx / file->zoom) / fullw(0);

			if (yoff < 0)
				yoff = 0;
			if (yoff >= maxyoff())
				yoff = maxyoff();

			if (xoff < -1)
				xoff = -1;
			if (xoff > 1)
				xoff = 1;
			if (file->mode != Z_CUSTOM)
				xoff = 0;

			lasty = my;
			lastx = mx;

			if (file->cache)
				updatevisible(yoff, w(), h(), false);
			redraw();
		}
		break;
		case FL_MOUSEWHEEL:
			if (Fl::event_ctrl()) {
				if (Fl::event_dy() > 0)
					cb_Zoomout(NULL, NULL);
				else
					cb_Zoomin(NULL, NULL);
			} else {
				yoff += move * Fl::event_dy();
				if (yoff < 0)
					yoff = 0;
				if (yoff >= maxyoff())
					yoff = maxyoff();
			}

			resetselection();
			if (file->cache)
				updatevisible(yoff, w(), h(), false);
			redraw();
		break;
		case FL_KEYDOWN:
		case FL_SHORTCUT:
			resetselection();
			switch (Fl::event_key()) {
				case 'k':
					if (Fl::event_ctrl()) {
						yoff = 0;
					} else {
						const u32 page = yoff;
						s32 sh = pxrel(page);
						s32 shp = sh;
						if (page > 0)
							shp = pxrel(page - 1);
						if (h() + 2 * MARGIN * file->zoom >= sh) {
							/* scroll up like Page_Up */
							if (floorf(yoff) + MARGIN * file->zoom / (float) sh >= yoff) {
								yoff = floorf(yoff - 1) + MARGIN * file->zoom / 2 / (float) shp;
							} else {
								yoff = floorf(yoff) + MARGIN * file->zoom / 2 / (float) sh;
							}
						} else {
							/* scroll up less than one page height */
							float d = (h() - MARGIN) / (float) sh;
							if (((u32) yoff) == ((u32) (yoff - d))) {
								/* not scrolling over page border */
								yoff -= d;
							} else {
								/* scrolling over page border */
								d -= (yoff - floorf(yoff));
								yoff = floorf(yoff);
								/* ratio of prev page can be different */
								d = d * (float) sh / (float) shp;
								yoff -= d;
							}
						}
						if (yoff < 0)
							yoff = 0;
						if (yoff >= maxyoff())
							yoff = maxyoff();
					}
					redraw();
				break;
				case 'j':
					if (Fl::event_ctrl()) {
						yoff = maxyoff();
					} else {
						const u32 page = yoff;
						s32 sh = pxrel(page);
						s32 shn = sh;
						if (page + 1 <= file->pages - 1)
							shn = pxrel(page + 1);
						if (h() + 2 * MARGIN * file->zoom >= sh) {
							/* scroll down like Page_Down */
							yoff = floorf(yoff + 1) + MARGIN * file->zoom / 2 / (float) shn;
						} else {
							/* scroll down less than one page height */
							float d = (h() - MARGIN) / (float) sh;
							if (((u32) yoff) == ((u32) (yoff + d))) {
								/* not scrolling over page border */
								yoff += d;
							} else {
								/* scrolling over page border */
								d -= (ceilf(yoff) - yoff);
								yoff = ceilf(yoff);
								/* ratio of next page can be different */
								d = d * (float) sh / (float) shn;
								yoff += d;
							}
						}
						if (yoff < 0)
							yoff = 0;
						if (yoff >= maxyoff())
							yoff = maxyoff();
					}
					redraw();
				break;
				case FL_Up:
					yoff -= move;
					if (yoff < 0)
						yoff = 0;
					redraw();
				break;
				case FL_Down:
					yoff += move;
					if (yoff >= maxyoff())
						yoff = maxyoff();
					redraw();
				break;
				case FL_Page_Up:
				{
					const u32 page = yoff;
					s32 sh = pxrel(page);
					s32 shp = sh;
					if (page)
						shp = pxrel(page - 1);
					if (floorf(yoff) + MARGIN * file->zoom / (float) sh >= yoff) {
						yoff = floorf(yoff - 1) + MARGIN * file->zoom / 2 / (float) shp;
					}
					else {
						yoff = floorf(yoff) + MARGIN * file->zoom / 2 / (float) sh;
					}
					if (yoff < 0)
						yoff = 0;
					redraw();
				}
				break;
				case FL_Page_Down:
				{
					u32 page = floorf(yoff);
					s32 shn = pxrel(page);
					if (page + 1 <= file->pages - 1)
						shn = pxrel(page + 1);
					yoff = floorf(yoff + 1) + MARGIN * file->zoom / 2 / (float) shn;
					if (yoff >= maxyoff())
						yoff = maxyoff();
					redraw();
				}
				break;
				case FL_Home:
					if (Fl::event_ctrl()) {
						yoff = 0;
					} else {
						yoff = floorf(yoff);
					}
					redraw();
				break;
				case FL_End:
					if (Fl::event_ctrl()) {
						yoff = maxyoff();
					} else {
						u32 page = yoff;
						s32 sh = pxrel(page);

						if (file->cache[page].ready) {
							const s32 hidden = sh - h();
							float tmp = floorf(yoff) + hidden / (float) sh;
							if (tmp > yoff)
								yoff = tmp;
							if (yoff < 0)
								yoff = 0;
							if (yoff >= maxyoff())
								yoff = maxyoff();
						} else {
							yoff = ceilf(yoff) - 0.4f;
						}
					}
					redraw();
				break;
				case FL_F + 8:
					cb_hide(NULL, NULL);
				break;
				default:
					return 0;
			}

			if (file->cache)
				updatevisible(yoff, w(), h(), false);

			return 1;
		break;
		case FL_MOVE:
			// Set the cursor appropriately
			if (!file->maxw)
				fl_cursor(FL_CURSOR_WAIT);
			else if (selecting->value())
				fl_cursor(FL_CURSOR_INSERT);
			else
				fl_cursor(FL_CURSOR_DEFAULT);
		break;
	}

	return Fl_Widget::handle(e);
}

u8 pdfview::iscached(const u32 page) const {
	u32 i;
	for (i = 0; i < CACHE_MAX; i++) {
		if (cachedpage[i] == page)
			return i;
	}

	return UCHAR_MAX;
}

void pdfview::docache(const u32 page) {

	// Insert it to cache. Pick the slot at random.
	const struct cachedpage * const cur = &file->cache[page];
	u32 i;

	if (cur->uncompressed > cachedsize) {
		cachedsize = cur->uncompressed;

		for (i = 0; i < CACHE_MAX; i++) {
			cache[i] = (u8 *) realloc(cache[i], cachedsize);
		}
	}

	// Be safe
	if (!cur->ready)
		return;

	const u32 dst = rand() % CACHE_MAX;

	lzo_uint dstsize = cachedsize;
	const int ret = lzo1x_decompress(cur->data,
					cur->size,
					cache[dst],
					&dstsize,
					NULL);
	if (ret != LZO_E_OK || dstsize != cur->uncompressed)
		die(_("Error decompressing\n"));

	cachedpage[dst] = page;

	// Create the Pixmap
	if (pix[dst] != None)
		XFreePixmap(fl_display, pix[dst]);

	pix[dst] = XCreatePixmap(fl_display, fl_window, cur->w, cur->h, 24);
	if (pix[dst] == None)
		return;

	fl_push_no_clip();

	XImage *xi = XCreateImage(fl_display, fl_visual->visual, 24, ZPixmap, 0,
					(char *) cache[dst], cur->w, cur->h,
					32, 0);
	if (xi == NULL) die("xi null\n");

	XPutImage(fl_display, pix[dst], fl_gc, xi, 0, 0, 0, 0, cur->w, cur->h);

	fl_pop_clip();

	xi->data = NULL;
	XDestroyImage(xi);
}

void pdfview::go(const u32 page) {
	yoff = page;
	resetselection();
	redraw();
}

void pdfview::content(const u32 page, const s32 X, const s32 Y,
			const u32 W, const u32 H) {

	// Do a gpu-accelerated bilinear blit
	u8 c = iscached(page);
	if (c == UCHAR_MAX)
		docache(page);
	c = iscached(page);

	const struct cachedpage * const cur = &file->cache[page];

	XRenderPictureAttributes srcattr;
	memset(&srcattr, 0, sizeof(XRenderPictureAttributes));
	XRenderPictFormat *fmt = XRenderFindStandardFormat(fl_display, PictStandardRGB24);

	// This corresponds to GL_CLAMP_TO_EDGE.
	srcattr.repeat = RepeatPad;

	Picture src = XRenderCreatePicture(fl_display, pix[c], fmt, CPRepeat, &srcattr);
	Picture dst = XRenderCreatePicture(fl_display, fl_window, fmt, 0, &srcattr);

	const Fl_Region clipr = fl_clip_region();
	XRenderSetPictureClipRegion(fl_display, dst, clipr);

	XRenderSetPictureFilter(fl_display, src, "bilinear", NULL, 0);
	XTransform xf;
	memset(&xf, 0, sizeof(XTransform));
	xf.matrix[0][0] = (65536 * cur->w) / W;
	xf.matrix[1][1] = (65536 * cur->h) / H;
	xf.matrix[2][2] = 65536;
	XRenderSetPictureTransform(fl_display, src, &xf);

	XRenderComposite(fl_display, PictOpSrc, src, None, dst, 0, 0, 0, 0, X, Y, W, H);
//	XCopyArea(fl_display, pix[c], fl_window, fl_gc, 0, 0, W, H, X, Y);
//	fl_draw_image(cache[c], X, Y, W, H, 4, file->cache[page].w * 4);

	if (selecting->value() && selx2 && sely2 && selx != selx2 && sely != sely2) {
		// Draw a selection rectangle over this area
		const XRenderColor col = {0, 0, 16384, 16384};

		u16 x, y, w, h;
		if (selx < selx2) {
			x = selx;
			w = selx2 - x;
		} else {
			x = selx2;
			w = selx - x;
		}

		if (sely < sely2) {
			y = sely;
			h = sely2 - y;
		} else {
			y = sely2;
			h = sely - y;
		}

		XRenderFillRectangle(fl_display, PictOpOver, dst, &col,
					x, y, w, h);
	}

	XRenderFreePicture(fl_display, src);
	XRenderFreePicture(fl_display, dst);
}
