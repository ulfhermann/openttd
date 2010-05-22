/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file widget_type.h Definitions about widgets. */

#ifndef WIDGET_TYPE_H
#define WIDGET_TYPE_H

#include "core/alloc_type.hpp"
#include "core/bitmath_func.hpp"
#include "strings_type.h"
#include "gfx_type.h"
#include "window_type.h"

static const int WIDGET_LIST_END = -1; ///< indicate the end of widgets' list for vararg functions

/** Bits of the #WWT_MATRIX widget data. */
enum MatrixWidgetValues {
	/* Number of column bits of the WWT_MATRIX widget data. */
	MAT_COL_START = 0, ///< Lowest bit of the number of columns.
	MAT_COL_BITS  = 8, ///< Number of bits for the number of columns in the matrix.

	/* Number of row bits of the WWT_MATRIX widget data. */
	MAT_ROW_START = 8, ///< Lowest bit of the number of rows.
	MAT_ROW_BITS  = 8, ///< Number of bits for the number of rows in the matrix.
};

/** Values for an arrow widget */
enum ArrowWidgetValues {
	AWV_DECREASE, ///< Arrow to the left or in case of RTL to the right
	AWV_INCREASE, ///< Arrow to the right or in case of RTL to the left
	AWV_LEFT,     ///< Force the arrow to the left
	AWV_RIGHT,    ///< Force the arrow to the right
};

/**
 * Window widget types, nested widget types, and nested widget part types.
 */
enum WidgetType {
	/* Window widget types. */
	WWT_EMPTY,      ///< Empty widget, place holder to reserve space in widget array

	WWT_PANEL,      ///< Simple depressed panel
	WWT_INSET,      ///< Pressed (inset) panel, most commonly used as combo box _text_ area
	WWT_IMGBTN,     ///< Button with image
	WWT_IMGBTN_2,   ///< Button with diff image when clicked

	WWT_TEXTBTN,    ///< Button with text
	WWT_TEXTBTN_2,  ///< Button with diff text when clicked
	WWT_LABEL,      ///< Centered label
	WWT_TEXT,       ///< Pure simple text
	WWT_MATRIX,     ///< Grid of rows and columns. @see MatrixWidgetValues
	WWT_SCROLLBAR,  ///< Vertical scrollbar
	WWT_FRAME,      ///< Frame
	WWT_CAPTION,    ///< Window caption (window title between closebox and stickybox)

	WWT_HSCROLLBAR, ///< Horizontal scrollbar
	WWT_SHADEBOX,   ///< Shade box (at top-right of a window, between caption and stickybox)
	WWT_STICKYBOX,  ///< Sticky box (normally at top-right of a window)
	WWT_DEBUGBOX,   ///< NewGRF debug box (between shade box and caption)
	WWT_SCROLL2BAR, ///< 2nd vertical scrollbar
	WWT_RESIZEBOX,  ///< Resize box (normally at bottom-right of a window)
	WWT_CLOSEBOX,   ///< Close box (at top-left of a window)
	WWT_DROPDOWN,   ///< Drop down list
	WWT_EDITBOX,    ///< a textbox for typing
	WWT_LAST,       ///< Last Item. use WIDGETS_END to fill up padding!!

	/* Nested widget types. */
	NWID_HORIZONTAL,      ///< Horizontal container.
	NWID_HORIZONTAL_LTR,  ///< Horizontal container that doesn't change the order of the widgets for RTL languages.
	NWID_VERTICAL,        ///< Vertical container.
	NWID_SPACER,          ///< Invisible widget that takes some space.
	NWID_SELECTION,       ///< Stacked widgets, only one visible at a time (eg in a panel with tabs).
	NWID_VIEWPORT,        ///< Nested widget containing a viewport.
	NWID_BUTTON_DROPDOWN, ///< Button with a drop-down.
	NWID_BUTTON_ARROW,    ///< Button with an arrow

	/* Nested widget part types. */
	WPT_RESIZE,       ///< Widget part for specifying resizing.
	WPT_MINSIZE,      ///< Widget part for specifying minimal size.
	WPT_MINTEXTLINES, ///< Widget part for specifying minimal number of lines of text.
	WPT_FILL,         ///< Widget part for specifying fill.
	WPT_DATATIP,      ///< Widget part for specifying data and tooltip.
	WPT_PADDING,      ///< Widget part for specifying a padding.
	WPT_PIPSPACE,     ///< Widget part for specifying pre/inter/post space for containers.
	WPT_ENDCONTAINER, ///< Widget part to denote end of a container.
	WPT_FUNCTION,     ///< Widget part for calling a user function.

	/* Pushable window widget types. */
	WWT_MASK = 0x7F,

	WWB_PUSHBUTTON  = 1 << 7,

	WWT_PUSHBTN     = WWT_PANEL   | WWB_PUSHBUTTON,
	WWT_PUSHTXTBTN  = WWT_TEXTBTN | WWB_PUSHBUTTON,
	WWT_PUSHIMGBTN  = WWT_IMGBTN  | WWB_PUSHBUTTON,
};

/** Different forms of sizing nested widgets, using NWidgetBase::AssignSizePosition() */
enum SizingType {
	ST_SMALLEST, ///< Initialize nested widget tree to smallest size. Also updates \e current_x and \e current_y.
	ST_RESIZE,   ///< Resize the nested widget tree.
};

/* Forward declarations. */
class NWidgetCore;
class Scrollbar;

/**
 * Baseclass for nested widgets.
 * @invariant After initialization, \f$current\_x = smallest\_x + n * resize\_x, for n \geq 0\f$.
 * @invariant After initialization, \f$current\_y = smallest\_y + m * resize\_y, for m \geq 0\f$.
 * @ingroup NestedWidgets
 */
class NWidgetBase : public ZeroedMemoryAllocator {
public:
	NWidgetBase(WidgetType tp);

	virtual void SetupSmallestSize(Window *w, bool init_array) = 0;
	virtual void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl) = 0;

	virtual void FillNestedArray(NWidgetBase **array, uint length) = 0;

	virtual NWidgetCore *GetWidgetFromPos(int x, int y) = 0;
	virtual NWidgetBase *GetWidgetOfType(WidgetType tp);

	/**
	 * Set additional space (padding) around the widget.
	 * @param top    Amount of additional space above the widget.
	 * @param right  Amount of additional space right of the widget.
	 * @param bottom Amount of additional space below the widget.
	 * @param left   Amount of additional space left of the widget.
	 */
	FORCEINLINE void SetPadding(uint8 top, uint8 right, uint8 bottom, uint8 left)
	{
		this->padding_top = top;
		this->padding_right = right;
		this->padding_bottom = bottom;
		this->padding_left = left;
	};

	FORCEINLINE uint GetHorizontalStepSize(SizingType sizing) const;
	FORCEINLINE uint GetVerticalStepSize(SizingType sizing) const;

	virtual void Draw(const Window *w) = 0;
	virtual void SetDirty(const Window *w) const;

	WidgetType type;      ///< Type of the widget / nested widget.
	uint fill_x;          ///< Horizontal fill stepsize (from initial size, \c 0 means not resizable).
	uint fill_y;          ///< Vertical fill stepsize (from initial size, \c 0 means not resizable).
	uint resize_x;        ///< Horizontal resize step (\c 0 means not resizable).
	uint resize_y;        ///< Vertical resize step (\c 0 means not resizable).
	/* Size of the widget in the smallest window possible.
	 * Computed by #SetupSmallestSize() followed by #AssignSizePosition().
	 */
	uint smallest_x;      ///< Smallest horizontal size of the widget in a filled window.
	uint smallest_y;      ///< Smallest vertical size of the widget in a filled window.
	/* Current widget size (that is, after resizing). */
	uint current_x;       ///< Current horizontal size (after resizing).
	uint current_y;       ///< Current vertical size (after resizing).

	uint pos_x;           ///< Horizontal position of top-left corner of the widget in the window.
	uint pos_y;           ///< Vertical position of top-left corner of the widget in the window.

	NWidgetBase *next;    ///< Pointer to next widget in container. Managed by parent container widget.
	NWidgetBase *prev;    ///< Pointer to previous widget in container. Managed by parent container widget.

	uint8 padding_top;    ///< Paddings added to the top of the widget. Managed by parent container widget.
	uint8 padding_right;  ///< Paddings added to the right of the widget. Managed by parent container widget.
	uint8 padding_bottom; ///< Paddings added to the bottom of the widget. Managed by parent container widget.
	uint8 padding_left;   ///< Paddings added to the left of the widget. Managed by parent container widget.

protected:
	FORCEINLINE void StoreSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height);
};

/**
 * Get the horizontal sizing step.
 * @param sizing Type of resize being performed.
 */
FORCEINLINE uint NWidgetBase::GetHorizontalStepSize(SizingType sizing) const
{
	return (sizing == ST_RESIZE) ? this->resize_x : this->fill_x;
}

/**
 * Get the vertical sizing step.
 * @param sizing Type of resize being performed.
 */
FORCEINLINE uint NWidgetBase::GetVerticalStepSize(SizingType sizing) const
{
	return (sizing == ST_RESIZE) ? this->resize_y : this->fill_y;
}

/**
 * Store size and position.
 * @param sizing       Type of resizing to perform.
 * @param x            Horizontal offset of the widget relative to the left edge of the window.
 * @param y            Vertical offset of the widget relative to the top edge of the window.
 * @param given_width  Width allocated to the widget.
 * @param given_height Height allocated to the widget.
 */
FORCEINLINE void NWidgetBase::StoreSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height)
{
	this->pos_x = x;
	this->pos_y = y;
	if (sizing == ST_SMALLEST) {
		this->smallest_x = given_width;
		this->smallest_y = given_height;
	}
	this->current_x = given_width;
	this->current_y = given_height;
}


/** Base class for a resizable nested widget.
 * @ingroup NestedWidgets */
class NWidgetResizeBase : public NWidgetBase {
public:
	NWidgetResizeBase(WidgetType tp, uint fill_x, uint fill_y);

	void SetMinimalSize(uint min_x, uint min_y);
	void SetMinimalTextLines(uint8 min_lines, uint8 spacing, FontSize size);
	void SetFill(uint fill_x, uint fill_y);
	void SetResize(uint resize_x, uint resize_y);

	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl);

	uint min_x; ///< Minimal horizontal size of only this widget.
	uint min_y; ///< Minimal vertical size of only this widget.
};

/** Nested widget flags that affect display and interaction withe 'real' widgets. */
enum NWidgetDisplay {
	/* Generic. */
	NDB_LOWERED         = 0, ///< Widget is lowered (pressed down) bit.
	NDB_DISABLED        = 1, ///< Widget is disabled (greyed out) bit.
	/* Viewport widget. */
	NDB_NO_TRANSPARENCY = 2, ///< Viewport is never transparent.
	NDB_SHADE_GREY      = 3, ///< Shade viewport to grey-scale.
	NDB_SHADE_DIMMED    = 4, ///< Display dimmed colours in the viewport.
	/* Button dropdown widget. */
	NDB_DROPDOWN_ACTIVE = 5, ///< Dropdown menu of the button dropdown widget is active. @see #NWID_BUTTON_DRPDOWN

	ND_LOWERED  = 1 << NDB_LOWERED,                ///< Bit value of the lowered flag.
	ND_DISABLED = 1 << NDB_DISABLED,               ///< Bit value of the disabled flag.
	ND_NO_TRANSPARENCY = 1 << NDB_NO_TRANSPARENCY, ///< Bit value of the 'no transparency' flag.
	ND_SHADE_GREY      = 1 << NDB_SHADE_GREY,      ///< Bit value of the 'shade to grey' flag.
	ND_SHADE_DIMMED    = 1 << NDB_SHADE_DIMMED,    ///< Bit value of the 'dimmed colours' flag.
	ND_DROPDOWN_ACTIVE = 1 << NDB_DROPDOWN_ACTIVE, ///< Bit value of the 'dropdown active' flag.
};
DECLARE_ENUM_AS_BIT_SET(NWidgetDisplay)

/** Base class for a 'real' widget.
 * @ingroup NestedWidgets */
class NWidgetCore : public NWidgetResizeBase {
public:
	NWidgetCore(WidgetType tp, Colours colour, uint fill_x, uint fill_y, uint16 widget_data, StringID tool_tip);

	void SetIndex(int index);
	void SetDataTip(uint16 widget_data, StringID tool_tip);

	inline void SetLowered(bool lowered);
	inline bool IsLowered() const;
	inline void SetDisabled(bool disabled);
	inline bool IsDisabled() const;

	/* virtual */ void FillNestedArray(NWidgetBase **array, uint length);
	/* virtual */ NWidgetCore *GetWidgetFromPos(int x, int y);

	virtual Scrollbar *FindScrollbar(Window *w, bool allow_next = true) const = 0;

	NWidgetDisplay disp_flags; ///< Flags that affect display and interaction with the widget.
	Colours colour;            ///< Colour of this widget.
	int index;                 ///< Index of the nested widget in the widget array of the window (\c -1 means 'not used').
	uint16 widget_data;        ///< Data of the widget. @see Widget::data
	StringID tool_tip;         ///< Tooltip of the widget. @see Widget::tootips
};

/**
 * Lower or raise the widget.
 * @param lowered Widget must be lowered (drawn pressed down).
 */
inline void NWidgetCore::SetLowered(bool lowered)
{
	this->disp_flags = lowered ? SETBITS(this->disp_flags, ND_LOWERED) : CLRBITS(this->disp_flags, ND_LOWERED);
}

/** Return whether the widget is lowered. */
inline bool NWidgetCore::IsLowered() const
{
	return HasBit(this->disp_flags, NDB_LOWERED);
}

/**
 * Disable (grey-out) or enable the widget.
 * @param disabled Widget must be disabled.
 */
inline void NWidgetCore::SetDisabled(bool disabled)
{
	this->disp_flags = disabled ? SETBITS(this->disp_flags, ND_DISABLED) : CLRBITS(this->disp_flags, ND_DISABLED);
}

/** Return whether the widget is disabled. */
inline bool NWidgetCore::IsDisabled() const
{
	return HasBit(this->disp_flags, NDB_DISABLED);
}


/** Baseclass for container widgets.
 * @ingroup NestedWidgets */
class NWidgetContainer : public NWidgetBase {
public:
	NWidgetContainer(WidgetType tp);
	~NWidgetContainer();

	void Add(NWidgetBase *wid);
	/* virtual */ void FillNestedArray(NWidgetBase **array, uint length);

	/** Return whether the container is empty. */
	inline bool IsEmpty() { return head == NULL; };

	/* virtual */ NWidgetBase *GetWidgetOfType(WidgetType tp);

protected:
	NWidgetBase *head; ///< Pointer to first widget in container.
	NWidgetBase *tail; ///< Pointer to last widget in container.
};

/** Display planes with zero size for #NWidgetStacked. */
enum StackedZeroSizePlanes {
	SZSP_VERTICAL = INT_MAX / 2, ///< Display plane with zero size horizontally, and filling and resizing vertically.
	SZSP_HORIZONTAL,             ///< Display plane with zero size vertically, and filling and resizing horizontally.
	SZSP_NONE,                   ///< Display plane with zero size in both directions (none filling and resizing).

	SZSP_BEGIN = SZSP_VERTICAL,  ///< First zero-size plane.
};

/** Stacked widgets, widgets all occupying the same space in the window.
 * #NWID_SELECTION allows for selecting one of several panels (planes) to tbe displayed. All planes must have the same size.
 * Since all planes are also initialized, switching between different planes can be done while the window is displayed.
 *
 * There are also a number of special planes (defined in #StackedZeroSizePlanes) that have zero size in one direction (and are stretchable in
 * the other direction) or have zero size in both directions. They are used to make all child planes of the widget disappear.
 * Unlike switching between the regular display planes (that all have the same size), switching from or to one of the zero-sized planes means that
 * a #Windows::ReInit() is needed to re-initialize the window since its size changes.
 */
class NWidgetStacked : public NWidgetContainer {
public:
	NWidgetStacked();

	void SetIndex(int index);

	void SetupSmallestSize(Window *w, bool init_array);
	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl);
	/* virtual */ void FillNestedArray(NWidgetBase **array, uint length);

	/* virtual */ void Draw(const Window *w);
	/* virtual */ NWidgetCore *GetWidgetFromPos(int x, int y);

	void SetDisplayedPlane(int plane);

	int shown_plane; ///< Plane being displayed (for #NWID_SELECTION only).
	int index;       ///< If non-negative, index in the #Window::nested_array.
};

/** Nested widget container flags, */
enum NWidContainerFlags {
	NCB_EQUALSIZE = 0, ///< Containers should keep all their (resizing) children equally large.

	NC_NONE = 0,                       ///< All flags cleared.
	NC_EQUALSIZE = 1 << NCB_EQUALSIZE, ///< Value of the #NCB_EQUALSIZE flag.
};
DECLARE_ENUM_AS_BIT_SET(NWidContainerFlags)

/** Container with pre/inter/post child space. */
class NWidgetPIPContainer : public NWidgetContainer {
public:
	NWidgetPIPContainer(WidgetType tp, NWidContainerFlags flags = NC_NONE);

	void SetPIP(uint8 pip_pre, uint8 pip_inter, uint8 pip_post);

	/* virtual */ void Draw(const Window *w);
	/* virtual */ NWidgetCore *GetWidgetFromPos(int x, int y);

protected:
	NWidContainerFlags flags; ///< Flags of the container.
	uint8 pip_pre;            ///< Amount of space before first widget.
	uint8 pip_inter;          ///< Amount of space between widgets.
	uint8 pip_post;           ///< Amount of space after last widget.
};

/** Horizontal container.
 * @ingroup NestedWidgets */
class NWidgetHorizontal : public NWidgetPIPContainer {
public:
	NWidgetHorizontal(NWidContainerFlags flags = NC_NONE);

	void SetupSmallestSize(Window *w, bool init_array);
	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl);
};

/** Horizontal container that doesn't change the direction of the widgets for RTL languages.
 * @ingroup NestedWidgets */
class NWidgetHorizontalLTR : public NWidgetHorizontal {
public:
	NWidgetHorizontalLTR(NWidContainerFlags flags = NC_NONE);

	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl);
};

/** Vertical container.
 * @ingroup NestedWidgets */
class NWidgetVertical : public NWidgetPIPContainer {
public:
	NWidgetVertical(NWidContainerFlags flags = NC_NONE);

	void SetupSmallestSize(Window *w, bool init_array);
	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl);
};


/** Spacer widget.
 * @ingroup NestedWidgets */
class NWidgetSpacer : public NWidgetResizeBase {
public:
	NWidgetSpacer(int length, int height);

	void SetupSmallestSize(Window *w, bool init_array);
	/* virtual */ void FillNestedArray(NWidgetBase **array, uint length);

	/* virtual */ void Draw(const Window *w);
	/* virtual */ void SetDirty(const Window *w) const;
	/* virtual */ NWidgetCore *GetWidgetFromPos(int x, int y);
};

/** Nested widget with a child.
 * @ingroup NestedWidgets */
class NWidgetBackground : public NWidgetCore {
public:
	NWidgetBackground(WidgetType tp, Colours colour, int index, NWidgetPIPContainer *child = NULL);
	~NWidgetBackground();

	void Add(NWidgetBase *nwid);
	void SetPIP(uint8 pip_pre, uint8 pip_inter, uint8 pip_post);

	void SetupSmallestSize(Window *w, bool init_array);
	void AssignSizePosition(SizingType sizing, uint x, uint y, uint given_width, uint given_height, bool rtl);

	/* virtual */ void FillNestedArray(NWidgetBase **array, uint length);

	/* virtual */ void Draw(const Window *w);
	/* virtual */ NWidgetCore *GetWidgetFromPos(int x, int y);
	/* virtual */ NWidgetBase *GetWidgetOfType(WidgetType tp);
	/* virtual */ Scrollbar *FindScrollbar(Window *w, bool allow_next = true) const;

private:
	NWidgetPIPContainer *child; ///< Child widget.
};

/**
 * Nested widget to display a viewport in a window.
 * After initializing the nested widget tree, call #InitializeViewport(). After changing the window size,
 * call #UpdateViewportCoordinates() eg from Window::OnResize().
 * If the #display_flags field contains the #ND_NO_TRANSPARENCY bit, the viewport will disable transparency.
 * Shading to grey-scale is controlled with the #ND_SHADE_GREY bit (used for B&W news papers), the #ND_SHADE_DIMMED gives dimmed colours (for colour news papers).
 * @todo Class derives from #NWidgetCore, but does not use #colour, #widget_data, or #tool_tip.
 * @ingroup NestedWidgets */
class NWidgetViewport : public NWidgetCore {
public:
	NWidgetViewport(int index);

	/* virtual */ void SetupSmallestSize(Window *w, bool init_array);
	/* virtual */ void Draw(const Window *w);
	/* virtual */ Scrollbar *FindScrollbar(Window *w, bool allow_next = true) const;

	void InitializeViewport(Window *w, uint32 follow_flags, ZoomLevel zoom);
	void UpdateViewportCoordinates(Window *w);
};

/** Leaf widget.
 * @ingroup NestedWidgets */
class NWidgetLeaf : public NWidgetCore {
public:
	NWidgetLeaf(WidgetType tp, Colours colour, int index, uint16 data, StringID tip);

	/* virtual */ void SetupSmallestSize(Window *w, bool init_array);
	/* virtual */ void Draw(const Window *w);
	/* virtual */ Scrollbar *FindScrollbar(Window *w, bool allow_next = true) const;

	bool ButtonHit(const Point &pt);

	static void InvalidateDimensionCache();
private:
	static Dimension shadebox_dimension;  ///< Cached size of a shadebox widget.
	static Dimension debugbox_dimension;  ///< Cached size of a debugbox widget.
	static Dimension stickybox_dimension; ///< Cached size of a stickybox widget.
	static Dimension resizebox_dimension; ///< Cached size of a resizebox widget.
	static Dimension closebox_dimension;  ///< Cached size of a closebox widget.
};

/**
 * Return the biggest possible size of a nested widget.
 * @param base      Base size of the widget.
 * @param max_space Available space for the widget.
 * @param step      Stepsize of the widget.
 * @return Biggest possible size of the widget, assuming that \a base may only be incremented by \a step size steps.
 */
static FORCEINLINE uint ComputeMaxSize(uint base, uint max_space, uint step)
{
	if (base >= max_space || step == 0) return base;
	if (step == 1) return max_space;
	uint increment = max_space - base;
	increment -= increment % step;
	return base + increment;
}

/**
 * @defgroup NestedWidgetParts Hierarchical widget parts
 * To make nested widgets easier to enter, nested widget parts have been created. They allow the tree to be defined in a flat array of parts.
 *
 * - Leaf widgets start with a #NWidget(WidgetType tp, Colours col, int16 idx) part.
 *   Next, specify its properties with one or more of
 *   - #SetMinimalSize Define the minimal size of the widget.
 *   - #SetFill Define how the widget may grow to make it nicely.
 *   - #SetDataTip Define the data and the tooltip of the widget.
 *   - #SetResize Define how the widget may resize.
 *   - #SetPadding Create additional space around the widget.
 *
 * - To insert a nested widget tree from an external source, nested widget part #NWidgetFunction exists.
 *   For further customization, the #SetPadding part may be used.
 *
 * - Space widgets (#NWidgetSpacer) start with a #NWidget(WidgetType tp), followed by one or more of
 *   - #SetMinimalSize Define the minimal size of the widget.
 *   - #SetFill Define how the widget may grow to make it nicely.
 *   - #SetResize Define how the widget may resize.
 *   - #SetPadding Create additional space around the widget.
 *
 * - Container widgets #NWidgetHorizontal, #NWidgetHorizontalLTR, and #NWidgetVertical, start with a #NWidget(WidgetType tp) part.
 *   Their properties are derived from the child widgets so they cannot be specified.
 *   You can however use
 *   - #SetPadding Define additional padding around the container.
 *   - #SetPIP Set additional pre/inter/post child widget space.
 *   .
 *   Underneath these properties, all child widgets of the container must be defined. To denote that they are childs, add an indent before the nested widget parts of
 *   the child widgets (it has no meaning for the compiler but it makes the widget parts easier to read).
 *   Below the last child widget, use an #EndContainer part. This part should be aligned with the #NWidget part that started the container.
 *
 * - Stacked widgets #NWidgetStacked map each of their childs onto the same space. It behaves like a container, except there is no pre/inter/post space,
 *   so the widget does not support #SetPIP. #SetPadding is allowed though.
 *   Like the other container widgets, below the last child widgets, a #EndContainer part should be used to denote the end of the stacked widget.
 *
 * - Background widgets #NWidgetBackground start with a #NWidget(WidgetType tp, Colours col, int16 idx) part.
 *   What follows depends on how the widget is used.
 *   - If the widget is used as a leaf widget, that is, to create some space in the window to display a viewport or some text, use the properties of the
 *     leaf widgets to define how it behaves.
 *   - If the widget is used a background behind other widgets, it is considered to be a container widgets. Use the properties listed there to define its
 *     behaviour.
 *   .
 *   In both cases, the background widget \b MUST end with a #EndContainer widget part.
 *
 * @see NestedWidgets
 */

/** Widget part for storing data and tooltip information.
 * @ingroup NestedWidgetParts */
struct NWidgetPartDataTip {
	uint16 data;      ///< Data value of the widget.
	StringID tooltip; ///< Tooltip of the widget.
};

/** Widget part for storing basic widget information.
 * @ingroup NestedWidgetParts */
struct NWidgetPartWidget {
	Colours colour; ///< Widget colour.
	int16 index;    ///< Widget index in the widget array.
};

/** Widget part for storing padding.
 * @ingroup NestedWidgetParts */
struct NWidgetPartPaddings {
	uint8 top, right, bottom, left; ///< Paddings for all directions.
};

/** Widget part for storing pre/inter/post spaces.
 * @ingroup NestedWidgetParts */
struct NWidgetPartPIP {
	uint8 pre, inter, post; ///< Amount of space before/between/after child widgets.
};

/** Widget part for storing minimal text line data.
 * @ingroup NestedWidgetParts */
struct NWidgetPartTextLines {
	uint8 lines;   ///< Number of text lines.
	uint8 spacing; ///< Extra spacing around lines.
	FontSize size; ///< Font size of text lines.
};

/** Pointer to function returning a nested widget.
 * @param biggest_index Pointer to storage for collecting the biggest index used in the nested widget.
 * @return Nested widget (tree).
 * @post \c *biggest_index must contain the value of the biggest index in the returned tree.
 */
typedef NWidgetBase *NWidgetFunctionType(int *biggest_index);

/** Partial widget specification to allow NWidgets to be written nested.
 * @ingroup NestedWidgetParts */
struct NWidgetPart {
	WidgetType type;                         ///< Type of the part. @see NWidgetPartType.
	union {
		Point xy;                        ///< Part with an x/y size.
		NWidgetPartDataTip data_tip;     ///< Part with a data/tooltip.
		NWidgetPartWidget widget;        ///< Part with a start of a widget.
		NWidgetPartPaddings padding;     ///< Part with paddings.
		NWidgetPartPIP pip;              ///< Part with pre/inter/post spaces.
		NWidgetPartTextLines text_lines; ///< Part with text line data.
		NWidgetFunctionType *func_ptr;   ///< Part with a function call.
		NWidContainerFlags cont_flags;   ///< Part with container flags.
	} u;
};

/**
 * Widget part function for setting the resize step.
 * @param dx Horizontal resize step. 0 means no horizontal resizing.
 * @param dy Vertical resize step. 0 means no vertical resizing.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetResize(int16 dx, int16 dy)
{
	NWidgetPart part;

	part.type = WPT_RESIZE;
	part.u.xy.x = dx;
	part.u.xy.y = dy;

	return part;
}

/**
 * Widget part function for setting the minimal size.
 * @param x Horizontal minimal size.
 * @param y Vertical minimal size.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetMinimalSize(int16 x, int16 y)
{
	NWidgetPart part;

	part.type = WPT_MINSIZE;
	part.u.xy.x = x;
	part.u.xy.y = y;

	return part;
}

/**
 * Widget part function for setting the minimal text lines.
 * @param lines   Number of text lines.
 * @param spacing Extra spacing required.
 * @param size    Font size of text.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetMinimalTextLines(uint8 lines, uint8 spacing, FontSize size = FS_NORMAL)
{
	NWidgetPart part;

	part.type = WPT_MINTEXTLINES;
	part.u.text_lines.lines = lines;
	part.u.text_lines.spacing = spacing;
	part.u.text_lines.size = size;

	return part;
}

/**
 * Widget part function for setting filling.
 * @param fill_x Horizontal filling step from minimal size.
 * @param fill_y Vertical filling step from minimal size.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetFill(uint fill_x, uint fill_y)
{
	NWidgetPart part;

	part.type = WPT_FILL;
	part.u.xy.x = fill_x;
	part.u.xy.y = fill_y;

	return part;
}

/**
 * Widget part function for denoting the end of a container
 * (horizontal, vertical, WWT_FRAME, WWT_INSET, or WWT_PANEL).
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart EndContainer()
{
	NWidgetPart part;

	part.type = WPT_ENDCONTAINER;

	return part;
}

/** Widget part function for setting the data and tooltip.
 * @param data Data of the widget.
 * @param tip  Tooltip of the widget.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetDataTip(uint16 data, StringID tip)
{
	NWidgetPart part;

	part.type = WPT_DATATIP;
	part.u.data_tip.data = data;
	part.u.data_tip.tooltip = tip;

	return part;
}

/**
 * Widget part function for setting additional space around a widget.
 * Parameters start above the widget, and are specified in clock-wise direction.
 * @param top The padding above the widget.
 * @param right The padding right of the widget.
 * @param bottom The padding below the widget.
 * @param left The padding left of the widget.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetPadding(uint8 top, uint8 right, uint8 bottom, uint8 left)
{
	NWidgetPart part;

	part.type = WPT_PADDING;
	part.u.padding.top = top;
	part.u.padding.right = right;
	part.u.padding.bottom = bottom;
	part.u.padding.left = left;

	return part;
}

/**
 * Widget part function for setting a padding.
 * @param padding The padding to use for all directions.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetPadding(uint8 padding)
{
	return SetPadding(padding, padding, padding, padding);
}

/**
 * Widget part function for setting a pre/inter/post spaces.
 * @param pre The amount of space before the first widget.
 * @param inter The amount of space between widgets.
 * @param post The amount of space after the last widget.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart SetPIP(uint8 pre, uint8 inter, uint8 post)
{
	NWidgetPart part;

	part.type = WPT_PIPSPACE;
	part.u.pip.pre = pre;
	part.u.pip.inter = inter;
	part.u.pip.post = post;

	return part;
}

/**
 * Widget part function for starting a new 'real' widget.
 * @param tp  Type of the new nested widget.
 * @param col Colour of the new widget.
 * @param idx Index of the widget in the widget array.
 * @note with #WWT_PANEL, #WWT_FRAME, #WWT_INSET, a new container is started.
 *       Child widgets must have a index bigger than the parent index.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart NWidget(WidgetType tp, Colours col, int16 idx = -1)
{
	NWidgetPart part;

	part.type = tp;
	part.u.widget.colour = col;
	part.u.widget.index = idx;

	return part;
}

/**
 * Widget part function for starting a new horizontal container, vertical container, or spacer widget.
 * @param tp         Type of the new nested widget, #NWID_HORIZONTAL(_LTR), #NWID_VERTICAL, #NWID_SPACER, or #NWID_SELECTION.
 * @param cont_flags Flags for the containers (#NWID_HORIZONTAL(_LTR) and #NWID_VERTICAL).
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart NWidget(WidgetType tp, NWidContainerFlags cont_flags = NC_NONE)
{
	NWidgetPart part;

	part.type = tp;
	part.u.cont_flags = cont_flags;

	return part;
}

/**
 * Obtain a nested widget (sub)tree from an external source.
 * @param func_ptr Pointer to function that returns the tree.
 * @ingroup NestedWidgetParts
 */
static inline NWidgetPart NWidgetFunction(NWidgetFunctionType *func_ptr)
{
	NWidgetPart part;

	part.type = WPT_FUNCTION;
	part.u.func_ptr = func_ptr;

	return part;
}

NWidgetContainer *MakeNWidgets(const NWidgetPart *parts, int count, int *biggest_index, NWidgetContainer *container);
NWidgetContainer *MakeWindowNWidgetTree(const NWidgetPart *parts, int count, int *biggest_index, NWidgetStacked **shade_select);

#endif /* WIDGET_TYPE_H */
