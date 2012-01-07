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


/** Types of cargo to display for station coverage. */
enum StationCoverageType {
	SCT_PASSENGERS_ONLY,     ///< Draw only passenger class cargoes.
	SCT_NON_PASSENGERS_ONLY, ///< Draw all non-passenger class cargoes.
	SCT_ALL,                 ///< Draw all cargoes.
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

/**
 * A cargo data entry representing one possible row in the station view window's
 * top part. Cargo data entries form a tree where each entry can have several
 * children. Parents keep track of the sums of their childrens' cargo counts.
 */
class CargoDataEntry {
public:
	CargoDataEntry();
	~CargoDataEntry();

	/**
	 * Insert a new child or retrieve an existing child using a station ID as ID.
	 * @param station ID of the station for which an entry shall be created or retrieved
	 * @return a child entry associated with the given station.
	 */
	CargoDataEntry *InsertOrRetrieve(StationID station) {return this->InsertOrRetrieve<StationID>(station);}

	/**
	 * Insert a new child or retrieve an existing child using a cargo ID as ID.
	 * @param cargo ID of the cargo for which an entry shall be created or retrieved
	 * @return a child entry associated with the given cargo.
	 */
	CargoDataEntry *InsertOrRetrieve(CargoID cargo) {return this->InsertOrRetrieve<CargoID>(cargo);}

	void Update(uint count);

	/**
	 * Remove a child associated with the given station.
	 * @param station ID of the station for which the child should be removed.
	 */
	void Remove(StationID station) {CargoDataEntry t(station); this->Remove(&t);}

	/**
	 * Remove a child associated with the given cargo.
	 * @param cargo ID of the cargo for which the child should be removed.
	 */
	void Remove(CargoID cargo) {CargoDataEntry t(cargo); this->Remove(&t);}

	/**
	 * Retrieve a child for the given station. Return NULL if it doesn't exist.
	 * @param station ID of the station the child we're looking for is associated with.
	 * @return a child entry for the given station or NULL.
	 */
	CargoDataEntry *Retrieve(StationID station) const {CargoDataEntry t(station); return this->Retrieve(this->children->find(&t));}

	/**
	 * Retrieve a child for the given cargo. Return NULL if it doesn't exist.
	 * @param cargo ID of the cargo the child we're looking for is associated with.
	 * @return a child entry for the given cargo or NULL.
	 */
	CargoDataEntry *Retrieve(CargoID cargo) const {CargoDataEntry t(cargo);return this->Retrieve(this->children->find(&t));}

	void Resort(CargoSortType type, SortOrder order);

	/**
	 * Get the station ID for this entry.
	 */
	StationID GetStation() const {return this->station;}

	/**
	 * Get the cargo ID for this entry.
	 */
	CargoID GetCargo() const {return this->cargo;}

	/**
	 * Get the cargo count for this entry.
	 */
	uint GetCount() const {return this->count;}

	/**
	 * Get the parent entry for this entry.
	 */
	CargoDataEntry *GetParent() const {return this->parent;}

	/**
	 * Get the number of children for this entry.
	 */
	uint GetNumChildren() const {return this->num_children;}

	/**
	 * Get an iterator pointing to the begin of the set of children.
	 */
	CargoDataSet::iterator Begin() const {return this->children->begin();}

	/**
	 * Get an iterator pointing to the end of the set of children.
	 */
	CargoDataSet::iterator End() const {return this->children->end();}

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

	CargoDataEntry *parent;   ///< the parent of this entry
	const union {
		StationID station;    ///< ID of the station this entry is associated with
		CargoID cargo;        ///< ID of the cargo this entry is associated with
	};
	uint num_children;        ///< the number of subentries belonging to this entry
	uint count;               ///< sum of counts of all children or amount of cargo for this entry
	CargoDataSet *children;   ///< the children of this entry
};

#endif /* STATION_GUI_H */
