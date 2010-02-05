/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gfx_type.h Types related to the graphics and/or input devices. */

#ifndef GFX_TYPE_H
#define GFX_TYPE_H

#include "core/endian_type.hpp"
#include "core/geometry_type.hpp"
#include "zoom_type.h"

typedef uint32 SpriteID;  ///< The number of a sprite, without mapping bits and colourtables
typedef uint32 PaletteID; ///< The number of the palette
typedef uint32 CursorID;  ///< The number of the cursor (sprite)

/** Combination of a palette sprite and a 'real' sprite */
struct PalSpriteID {
	SpriteID sprite;  ///< The 'real' sprite
	PaletteID pal;    ///< The palette (use \c PAL_NONE) if not needed)
};

enum WindowKeyCodes {
	WKC_SHIFT = 0x8000,
	WKC_CTRL  = 0x4000,
	WKC_ALT   = 0x2000,
	WKC_META  = 0x1000,

	/* Special ones */
	WKC_NONE        =  0,
	WKC_ESC         =  1,
	WKC_BACKSPACE   =  2,
	WKC_INSERT      =  3,
	WKC_DELETE      =  4,

	WKC_PAGEUP      =  5,
	WKC_PAGEDOWN    =  6,
	WKC_END         =  7,
	WKC_HOME        =  8,

	/* Arrow keys */
	WKC_LEFT        =  9,
	WKC_UP          = 10,
	WKC_RIGHT       = 11,
	WKC_DOWN        = 12,

	/* Return & tab */
	WKC_RETURN      = 13,
	WKC_TAB         = 14,

	/* Space */
	WKC_SPACE       = 32,

	/* Function keys */
	WKC_F1          = 33,
	WKC_F2          = 34,
	WKC_F3          = 35,
	WKC_F4          = 36,
	WKC_F5          = 37,
	WKC_F6          = 38,
	WKC_F7          = 39,
	WKC_F8          = 40,
	WKC_F9          = 41,
	WKC_F10         = 42,
	WKC_F11         = 43,
	WKC_F12         = 44,

	/* Backquote is the key left of "1"
	 * we only store this key here, no matter what character is really mapped to it
	 * on a particular keyboard. (US keyboard: ` and ~ ; German keyboard: ^ and °) */
	WKC_BACKQUOTE   = 45,
	WKC_PAUSE       = 46,

	/* 0-9 are mapped to 48-57
	 * A-Z are mapped to 65-90
	 * a-z are mapped to 97-122 */

	/* Numerical keyboard */
	WKC_NUM_DIV     = 138,
	WKC_NUM_MUL     = 139,
	WKC_NUM_MINUS   = 140,
	WKC_NUM_PLUS    = 141,
	WKC_NUM_ENTER   = 142,
	WKC_NUM_DECIMAL = 143,

	/* Other keys */
	WKC_SLASH       = 144, ///< / Forward slash
	WKC_SEMICOLON   = 145, ///< ; Semicolon
	WKC_EQUALS      = 146, ///< = Equals
	WKC_L_BRACKET   = 147, ///< [ Left square bracket
	WKC_BACKSLASH   = 148, ///< \ Backslash
	WKC_R_BRACKET   = 149, ///< ] Right square bracket
	WKC_SINGLEQUOTE = 150, ///< ' Single quote
	WKC_COMMA       = 151, ///< , Comma
	WKC_PERIOD      = 152, ///< . Period
	WKC_MINUS       = 153, ///< - Minus
};

/** A single sprite of a list of animated cursors */
struct AnimCursor {
	static const CursorID LAST = MAX_UVALUE(CursorID);
	CursorID sprite;   ///< Must be set to LAST_ANIM when it is the last sprite of the loop
	byte display_time; ///< Amount of ticks this sprite will be shown
};

/** Collection of variables for cursor-display and -animation */
struct CursorVars {
	Point pos, size, offs, delta; ///< position, size, offset from top-left, and movement
	Point draw_pos, draw_size;    ///< position and size bounding-box for drawing
	int short_vehicle_offset;     ///< offset of the X for short vehicles
	CursorID sprite; ///< current image of cursor
	PaletteID pal;

	int wheel;       ///< mouse wheel movement

	/* We need two different vars to keep track of how far the scrollwheel moved.
	 * OSX uses this for scrolling around the map. */
	int v_wheel;
	int h_wheel;

	const AnimCursor *animate_list; ///< in case of animated cursor, list of frames
	const AnimCursor *animate_cur;  ///< in case of animated cursor, current frame
	uint animate_timeout;           ///< in case of animated cursor, number of ticks to show the current cursor

	bool visible;    ///< cursor is visible
	bool dirty;      ///< the rect occupied by the mouse is dirty (redraw)
	bool fix_at;     ///< mouse is moving, but cursor is not (used for scrolling)
	bool in_window;  ///< mouse inside this window, determines drawing logic

	bool vehchain;   ///< vehicle chain is dragged
};

/** Data about how and where to blit pixels. */
struct DrawPixelInfo {
	void *dst_ptr;
	int left, top, width, height;
	int pitch;
	ZoomLevel zoom;
};

/** Structure to access the alpha, red, green, and blue channels from a 32 bit number. */
union Colour {
	uint32 data; ///< Conversion of the channel information to a 32 bit number.
	struct {
#if TTD_ENDIAN == TTD_BIG_ENDIAN
		uint8 a, r, g, b; ///< colour channels in BE order
#else
		uint8 b, g, r, a; ///< colour channels in LE order
#endif /* TTD_ENDIAN == TTD_BIG_ENDIAN */
	};
};

/** Available font sizes */
enum FontSize {
	FS_NORMAL, ///< Index of the normal font in the font tables.
	FS_SMALL,  ///< Index of the small font in the font tables.
	FS_LARGE,  ///< Index of the large font in the font tables.
	FS_END,

	FS_BEGIN = FS_NORMAL, ///< First font.
};
DECLARE_POSTFIX_INCREMENT(FontSize);

/**
 * Used to only draw a part of the sprite.
 * Draw the subsprite in the rect (sprite_x_offset + left, sprite_y_offset + top) to (sprite_x_offset + right, sprite_y_offset + bottom).
 * Both corners are included in the drawing area.
 */
struct SubSprite {
	int left, top, right, bottom;
};

enum Colours {
	COLOUR_DARK_BLUE,
	COLOUR_PALE_GREEN,
	COLOUR_PINK,
	COLOUR_YELLOW,
	COLOUR_RED,
	COLOUR_LIGHT_BLUE,
	COLOUR_GREEN,
	COLOUR_DARK_GREEN,
	COLOUR_BLUE,
	COLOUR_CREAM,
	COLOUR_MAUVE,
	COLOUR_PURPLE,
	COLOUR_ORANGE,
	COLOUR_BROWN,
	COLOUR_GREY,
	COLOUR_WHITE,
	COLOUR_END,
	INVALID_COLOUR = 0xFF,
};

/** Colour of the strings, see _string_colourmap in table/palettes.h or docs/ottd-colourtext-palette.png */
enum TextColour {
	TC_FROMSTRING  = 0x00,
	TC_BLUE        = 0x00,
	TC_SILVER      = 0x01,
	TC_GOLD        = 0x02,
	TC_RED         = 0x03,
	TC_PURPLE      = 0x04,
	TC_LIGHT_BROWN = 0x05,
	TC_ORANGE      = 0x06,
	TC_GREEN       = 0x07,
	TC_YELLOW      = 0x08,
	TC_DARK_GREEN  = 0x09,
	TC_CREAM       = 0x0A,
	TC_BROWN       = 0x0B,
	TC_WHITE       = 0x0C,
	TC_LIGHT_BLUE  = 0x0D,
	TC_GREY        = 0x0E,
	TC_DARK_BLUE   = 0x0F,
	TC_BLACK       = 0x10,
	TC_INVALID     = 0xFF,

	IS_PALETTE_COLOUR = 0x100, ///< colour value is already a real palette colour index, not an index of a StringColour
};
DECLARE_ENUM_AS_BIT_SET(TextColour);

/** Defines a few values that are related to animations using palette changes */
enum PaletteAnimationSizes {
	PALETTE_ANIM_SIZE_WIN   = 28,   ///< number of animated colours in Windows palette
	PALETTE_ANIM_SIZE_DOS   = 38,   ///< number of animated colours in DOS palette
	PALETTE_ANIM_SIZE_START = 217,  ///< Index in  the _palettes array from which all animations are taking places (table/palettes.h)
};

/** Define the operation GfxFillRect performs */
enum FillRectMode {
	FILLRECT_OPAQUE,  ///< Fill rectangle with a single colour
	FILLRECT_CHECKER, ///< Draw only every second pixel, used for greying-out
	FILLRECT_RECOLOUR, ///< Apply a recolour sprite to the screen content
};

/** Palettes OpenTTD supports. */
enum PaletteType {
	PAL_DOS,        ///< Use the DOS palette.
	PAL_WINDOWS,    ///< Use the Windows palette.
	PAL_AUTODETECT, ///< Automatically detect the palette based on the graphics pack.
	MAX_PAL = 2,    ///< The number of palettes.
};

/** Types of sprites that might be loaded */
enum SpriteType {
	ST_NORMAL   = 0,      ///< The most basic (normal) sprite
	ST_MAPGEN   = 1,      ///< Special sprite for the map generator
	ST_FONT     = 2,      ///< A sprite used for fonts
	ST_RECOLOUR = 3,      ///< Recolour sprite
	ST_INVALID  = 4,      ///< Pseudosprite or other unusable sprite, used only internally
};

#endif /* GFX_TYPE_H */
