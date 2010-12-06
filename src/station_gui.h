/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_gui.h Contains enums and function declarations connected with stations GUI */

#ifndef STATION_GUI_H
#define STATION_GUI_H

#include "command_type.h"
#include "station_type.h"
#include "tilearea_type.h"
#include "window_type.h"
#include "cargo_type.h"
#include <set>

/** Enum for StationView, referring to _station_view_widgets and _station_view_expanded_widgets */
enum StationViewWidgets {
	SVW_CAPTION    =  0, ///< Caption of the window
	SVW_SORT_ORDER =  1, ///< 'Sort order' button
	SVW_SORT_BY    =  2, ///< 'Sort by' button
	SVW_MODE       =  3, ///< button for toggling planned and real flows
	SVW_GROUP_BY   =  4, ///< 'Group by' button
	SVW_WAITING    =  5, ///< List of waiting cargo
	SVW_SCROLLBAR  =  6, ///< Scrollbar
	SVW_ACCEPTLIST =  7, ///< List of accepted cargos
	SVW_RATINGLIST =  7, ///< Ratings of cargos
	SVW_LOCATION   =  8, ///< 'Location' button
	SVW_RATINGS    =  9, ///< 'Ratings' button
	SVW_ACCEPTS    =  9, ///< 'Accepts' button
	SVW_RENAME     = 10, ///< 'Rename' button
	SVW_TRAINS     = 11, ///< List of scheduled trains button
	SVW_ROADVEHS,        ///< List of scheduled road vehs button
	SVW_SHIPS,           ///< List of scheduled ships button
	SVW_PLANES,          ///< List of scheduled planes button
};

/** Types of cargo to display for station coverage. */
enum StationCoverageType {
	SCT_PASSENGERS_ONLY,     ///< Draw only passenger class cargos.
	SCT_NON_PASSENGERS_ONLY, ///< Draw all non-passenger class cargos.
	SCT_ALL,                 ///< Draw all cargos.
};

int DrawStationCoverageAreaText(int left, int right, int top, StationCoverageType sct, int rad, bool supplies);
void CheckRedrawStationCoverage(const Window *w);

void ShowSelectStationIfNeeded(CommandContainer cmd, TileArea ta);
void ShowSelectWaypointIfNeeded(CommandContainer cmd, TileArea ta);

enum SortOrder {
	SO_DESCENDING,
	SO_ASCENDING
};

class CargoDataEntry;

enum CargoSortType {
	ST_AS_GROUPING,    ///< by the same principle the entries are being grouped
	ST_COUNT,          ///< by amount of cargo
	ST_STATION_STRING, ///< by station name
	ST_STATION_ID,     ///< by station id
	ST_CARGO_ID,       ///< by cargo id
};

class CargoSorter {
public:
	CargoSorter(CargoSortType t = ST_STATION_ID, SortOrder o = SO_ASCENDING) : type(t), order(o) {}
	CargoSortType GetSortType() {return this->type;}
	bool operator()(const CargoDataEntry *cd1, const CargoDataEntry *cd2) const;

private:
	CargoSortType type;
	SortOrder order;

	template<class ID>
	bool SortId(ID st1, ID st2) const;
	bool SortCount(const CargoDataEntry *cd1, const CargoDataEntry *cd2) const;
	bool SortStation (StationID st1, StationID st2) const;
};

typedef std::set<CargoDataEntry *, CargoSorter> CargoDataSet;

class CargoDataEntry {
public:
	CargoDataEntry();
	~CargoDataEntry();

	CargoDataEntry *InsertOrRetrieve(StationID s) {return this->InsertOrRetrieve<StationID>(s);}
	CargoDataEntry *InsertOrRetrieve(CargoID car) {return this->InsertOrRetrieve<CargoID>(car);}
	void Update(uint count);

	void Remove(StationID s) {CargoDataEntry t(s); this->Remove(&t);}
	void Remove(CargoID c) {CargoDataEntry t(c); this->Remove(&t);}

	CargoDataEntry *Retrieve(StationID s) const {CargoDataEntry t(s); return this->Retrieve(this->subentries->find(&t));}
	CargoDataEntry *Retrieve(CargoID c) const {CargoDataEntry t(c);return this->Retrieve(this->subentries->find(&t));}

	void Resort(CargoSortType type, SortOrder order);

	StationID GetStation() const {return this->station;}
	CargoID GetCargo() const {return this->cargo;}
	uint GetCount() const {return this->count;}
	CargoDataEntry *GetParent() const {return this->parent;}
	uint Size() const {return this->size;}

	CargoDataSet::iterator Begin() const {return this->subentries->begin();}
	CargoDataSet::iterator End() const {return this->subentries->end();}

	void Clear();
private:

	CargoDataEntry(StationID st, uint c, CargoDataEntry *p);
	CargoDataEntry(CargoID car, uint c, CargoDataEntry *p);
	CargoDataEntry(StationID st);
	CargoDataEntry(CargoID car);

	CargoDataEntry *Retrieve(CargoDataSet::iterator i) const;

	template<class ID>
	CargoDataEntry *InsertOrRetrieve(ID s);

	void Remove(CargoDataEntry *comp);
	void IncrementSize();

	CargoDataEntry *parent;
	const union {
		StationID station;
		CargoID cargo;
	};
	uint size;
	uint count;
	CargoDataSet *subentries;
};

#endif /* STATION_GUI_H */
