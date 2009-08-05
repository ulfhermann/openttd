/* $Id$ */

/** @file zoom_type.h Types related to zooming in and out. */

#ifndef ZOOM_TYPE_H
#define ZOOM_TYPE_H

#include "core/enum_type.hpp"

enum ZoomLevel {
	/* Our possible zoom-levels */
	ZOOM_LVL_IN_8X,
	ZOOM_LVL_IN_4X,
	ZOOM_LVL_IN_2X,
	ZOOM_LVL_NORMAL,
	ZOOM_LVL_OUT_2X,
	ZOOM_LVL_OUT_4X,
	ZOOM_LVL_OUT_8X,

	/* Here we define in which zoom viewports are */
	ZOOM_LVL_VIEWPORT = ZOOM_LVL_NORMAL,
	ZOOM_LVL_NEWS     = ZOOM_LVL_NORMAL,
	ZOOM_LVL_INDUSTRY = ZOOM_LVL_OUT_2X,
	ZOOM_LVL_TOWN     = ZOOM_LVL_OUT_2X,
	ZOOM_LVL_AIRCRAFT = ZOOM_LVL_NORMAL,
	ZOOM_LVL_SHIP     = ZOOM_LVL_NORMAL,
	ZOOM_LVL_TRAIN    = ZOOM_LVL_NORMAL,
	ZOOM_LVL_ROADVEH  = ZOOM_LVL_NORMAL,
	ZOOM_LVL_WORLD_SCREENSHOT = ZOOM_LVL_NORMAL,

	ZOOM_LVL_DETAIL   = ZOOM_LVL_OUT_2X, ///< All zoomlevels with higher resolution or equal to this, will result in details on the screen, like road-work, ...

	/* min/max for all zoom levels */
	ZOOM_LVL_MIN      = ZOOM_LVL_IN_8X,
	ZOOM_LVL_MAX      = ZOOM_LVL_OUT_8X,
	ZOOM_LVL_COUNT    = ZOOM_LVL_MAX + 1 - ZOOM_LVL_MIN,

	/* min/max for zoom levels the blitter can handle
	 *
	 * This distinction makes it possible to introduce more zoom levels for other windows.
	 * For example the smallmap is drawn independently from the main viewport and thus
	 * could support different zoom levels.
	 */
	ZOOM_LVL_BLITTER_MIN   = ZOOM_LVL_NORMAL,
	ZOOM_LVL_BLITTER_MAX   = ZOOM_LVL_OUT_8X,
	ZOOM_LVL_BLITTER_COUNT = ZOOM_LVL_BLITTER_MAX + 1 - ZOOM_LVL_BLITTER_MIN,
};
DECLARE_POSTFIX_INCREMENT(ZoomLevel)

#endif /* ZOOM_TYPE_H */
