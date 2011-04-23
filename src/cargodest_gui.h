/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_gui.h Functions/types related to the cargodest GUI. */

#ifndef CARGODEST_GUI_H
#define CARGODEST_GUI_H

#include "cargodest_base.h"
#include "sortlist_type.h"

/** Helper encapsulating a #CargoLink. */
struct GUICargoLink {
	CargoID         cid;   ///< Cargo ID of this link.
	const CargoLink *link; ///< Pointer to the link.

	GUICargoLink(CargoID c, const CargoLink *l) : cid(c), link(l) {}
};

/** Sorted list of demand destinations for displaying. */
class CargoDestinationList
{
private:
	const CargoSourceSink *obj;      ///< The object which destinations are displayed.
	GUIList<GUICargoLink> link_list; ///< Sorted list of destinations.

	void RebuildList();
	void SortList();

public:
	CargoDestinationList(const CargoSourceSink *o);

	void InvalidateData();
	void Resort();

	uint GetListHeight() const;
	uint DrawList(uint left, uint right, uint y) const;
};

#endif /* CARGODEST_GUI_H */
