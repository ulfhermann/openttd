/* $Id$ */

/** @file station_gui.h Contains enums and function declarations connected with stations GUI */

#ifndef STATION_GUI_H
#define STATION_GUI_H

#include "command_type.h"
#include "station_type.h"
#include <set>

/** Enum for StationView, referring to _station_view_widgets and _station_view_expanded_widgets */
enum StationViewWidgets {
	SVW_CLOSEBOX   =  0, ///< Close 'X' button
	SVW_CAPTION    =  1, ///< Caption of the window
	SVW_WAITING    =  3, ///< List of waiting cargo
	SVW_ACCEPTLIST =  5, ///< List of accepted cargos
	SVW_RATINGLIST =  5, ///< Ratings of cargos
	SVW_LOCATION   =  6, ///< 'Location' button
	SVW_RATINGS    =  7, ///< 'Ratings' button
	SVW_ACCEPTS    =  7, ///< 'Accepts' button
	SVW_FLOWS      =  8, ///< button for toggling planned and real flows
	SVW_RENAME     =  9, ///< 'Rename' button
	SVW_TRAINS     = 10, ///< List of scheduled trains button
	SVW_ROADVEHS,        ///< List of scheduled road vehs button
	SVW_PLANES,          ///< List of scheduled planes button
	SVW_SHIPS,           ///< List of scheduled ships button
	SVW_RESIZE,          ///< Resize button
};

enum StationCoverageType {
	SCT_PASSENGERS_ONLY,
	SCT_NON_PASSENGERS_ONLY,
	SCT_ALL
};

int DrawStationCoverageAreaText(int sx, int sy, StationCoverageType sct, int rad, bool supplies);
void CheckRedrawStationCoverage(const Window *w);

void ShowSelectStationIfNeeded(CommandContainer cmd, int w, int h);

enum SortOrder {
	SO_DESCENDING,
	SO_ASCENDING
};

enum SortType {
	ST_STATION,
	ST_STATION_ID,
	ST_CARGO_ID,
	ST_COUNT,
};

class CargoDataEntry;

class CargoSorter {
public:
	CargoSorter(SortType t = ST_STATION_ID, SortOrder o = SO_ASCENDING) : type(t), order(o) {}
	SortType GetSortType() {return type;}
	bool operator()(const CargoDataEntry * cd1, const CargoDataEntry * cd2) const;

private:
	SortType type;
	SortOrder order;

	template<class ID>
	bool SortId(ID st1, ID st2) const;

	bool SortStation (StationID st1, StationID st2) const;
};

typedef std::set<CargoDataEntry *, CargoSorter> CargoDataSet;

class CargoDataEntry {
public:
	CargoDataEntry();
	~CargoDataEntry();

	CargoDataEntry * Update(StationID s, uint c = 0) {return Update<StationID>(s, c);}
	CargoDataEntry * Update(CargoID car, uint c = 0) {return Update<CargoID>(car, c);}

	void Remove(StationID s) {CargoDataEntry t(s); subentries->erase(&t);}
	void Remove(CargoID c) {CargoDataEntry t(c); subentries->erase(&t);}

	CargoDataEntry * Retrieve(StationID s) const {CargoDataEntry t(s); return Retrieve(subentries->find(&t));}
	CargoDataEntry * Retrieve(CargoID c) const {CargoDataEntry t(c);return Retrieve(subentries->find(&t));}

	void Resort(SortType type, SortOrder order);

	StationID GetStation() const {return station;}
	CargoID GetCargo() const {return cargo;}
	uint GetCount() const {return count;}
	CargoDataEntry * GetParent() const {return parent;}
	uint Size() const {return size;}

	CargoDataSet::const_iterator Begin() const {return subentries->begin();}
	CargoDataSet::const_iterator End() const {return subentries->end();}



private:

	CargoDataEntry(StationID st, uint c, CargoDataEntry * p);
	CargoDataEntry(CargoID car, uint c, CargoDataEntry * p);
	CargoDataEntry(StationID s);
	CargoDataEntry(CargoID c);
	CargoDataEntry * Retrieve(CargoDataSet::iterator i) const;
	template<class ID>
	CargoDataEntry * Update(ID s, uint c);
	void IncrementSize();
	CargoDataEntry * parent;
	const union {
		StationID station;
		CargoID cargo;
	};
	uint size;
	uint count;
	CargoDataSet * subentries;
};

#endif /* STATION_GUI_H */
