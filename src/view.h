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

#ifndef VIEW_H
#define VIEW_H

#include "main.h"

#define CACHE_MAX 3

class pdfview: public Fl_Widget {
public:
	pdfview(int x, int y, int w, int h);
	void draw();
	int handle(int e);

	void go(const u32 page);
private:
	u8 iscached(const u32 page) const;
	void docache(const u32 page);

	float xoff, yoff;
	u32 cachedsize;
	u8 *cache[CACHE_MAX];
	u16 cachedpage[CACHE_MAX];
};

#endif
