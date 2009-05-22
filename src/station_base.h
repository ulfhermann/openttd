/* $Id$ */

/** @file station_base.h Base classes/functions for stations. */

#ifndef STATION_BASE_H
#define STATION_BASE_H

#include "station_type.h"
#include "airport.h"
#include "core/pool_type.hpp"
#include "cargopacket.h"
#include "cargo_type.h"
#include "town_type.h"
#include "strings_type.h"
#include "date_type.h"
#include "vehicle_type.h"
#include "company_type.h"
#include "industry_type.h"
#include "core/geometry_type.hpp"
#include "viewport_type.h"
#include "linkgraph/linkgraph_types.h"
#include <list>
#include <map>
#include <set>

typedef Pool<Station, StationID, 32, 64000> StationPool;
typedef Pool<RoadStop, RoadStopID, 32, 64000> RoadStopPool;
extern StationPool _station_pool;
extern RoadStopPool _roadstop_pool;

static const byte INITIAL_STATION_RATING = 175;

class LinkStat {
public:
	uint capacity;
	uint frozen;
	uint usage;
	LinkStat() : capacity(0), frozen(0), usage(0) {}

	inline LinkStat & operator*=(uint factor) {
		capacity *= factor;
		usage *= factor;
		return *this;
	}

	inline LinkStat & operator/=(uint divident) {
		capacity /= divident;
		if (capacity < frozen) {
			capacity = frozen;
		}
		usage /= divident;
		return *this;
	}
};

class FlowStat {
public:
	FlowStat(StationID st = INVALID_STATION, uint p = 0, uint s = 0) :
		planned(p), sent(s), via(st) {}
	uint planned;
	uint sent;
	StationID via;
	struct comp {
		bool operator()(const FlowStat & x, const FlowStat & y) const {
			int diff_x = (int)x.planned - (int)x.sent;
			int diff_y = (int)y.planned - (int)y.sent;
			if (diff_x != diff_y) {
				return diff_x > diff_y;
			} else {
				return x.via > y.via;
			}
		}
	};

};

typedef std::set<FlowStat, FlowStat::comp> FlowStatSet; ///< percentage of flow to be sent via specified station (or consumed locally)
typedef std::map<StationID, LinkStat> LinkStatMap;
typedef std::map<StationID, FlowStatSet> FlowStatMap; ///< flow descriptions by origin stations

struct GoodsEntry {
	enum AcceptancePickup {
		ACCEPTANCE,
		PICKUP
	};

	GoodsEntry() :
		acceptance_pickup(0),
		days_since_pickup(255),
		rating(INITIAL_STATION_RATING),
		last_speed(0),
		last_age(255)
	{}

	byte acceptance_pickup;
	byte days_since_pickup;
	byte rating;
	byte last_speed;
	byte last_age;
	CargoList cargo;        ///< The cargo packets of cargo waiting in this station
	uint supply;
	FlowStatMap flows;      ///< The planned flows through this station
	LinkStatMap link_stats; ///< capacities and usage statistics for outgoing links
	LinkGraphComponentID last_component; ///< the component this station was last part of in this cargo's link graph
};

/** A Stop for a Road Vehicle */
struct RoadStop : RoadStopPool::PoolItem<&_roadstop_pool> {
	static const int  cDebugCtorLevel =  5;  ///< Debug level on which Contructor / Destructor messages are printed
	static const uint LIMIT           = 16;  ///< The maximum amount of roadstops that are allowed at a single station
	static const uint MAX_BAY_COUNT   =  2;  ///< The maximum number of loading bays
	static const uint MAX_VEHICLES    = 64;  ///< The maximum number of vehicles that can allocate a slot to this roadstop

	TileIndex        xy;                    ///< Position on the map
	byte             status;                ///< Current status of the Stop. Like which spot is taken. Access using *Bay and *Busy functions.
	byte             num_vehicles;          ///< Number of vehicles currently slotted to this stop
	struct RoadStop  *next;                 ///< Next stop of the given type at this station

	RoadStop(TileIndex tile = INVALID_TILE);
	~RoadStop();

	/* For accessing status */
	bool HasFreeBay() const;
	bool IsFreeBay(uint nr) const;
	uint AllocateBay();
	void AllocateDriveThroughBay(uint nr);
	void FreeBay(uint nr);
	bool IsEntranceBusy() const;
	void SetEntranceBusy(bool busy);

	RoadStop *GetNextRoadStop(const Vehicle *v) const;
};

struct StationSpecList {
	const StationSpec *spec;
	uint32 grfid;      ///< GRF ID of this custom station
	uint8  localidx;   ///< Station ID within GRF of station
};

/** StationRect - used to track station spread out rectangle - cheaper than scanning whole map */
struct StationRect : public Rect {
	enum StationRectMode
	{
		ADD_TEST = 0,
		ADD_TRY,
		ADD_FORCE
	};

	StationRect();
	void MakeEmpty();
	bool PtInExtendedRect(int x, int y, int distance = 0) const;
	bool IsEmpty() const;
	bool BeforeAddTile(TileIndex tile, StationRectMode mode);
	bool BeforeAddRect(TileIndex tile, int w, int h, StationRectMode mode);
	bool AfterRemoveTile(Station *st, TileIndex tile);
	bool AfterRemoveRect(Station *st, TileIndex tile, int w, int h);

	static bool ScanForStationTiles(StationID st_id, int left_a, int top_a, int right_a, int bottom_a);

	StationRect& operator = (Rect src);
};

/** Station data structure */
struct Station : StationPool::PoolItem<&_station_pool> {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == ROADSTOP_BUS ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const Vehicle *v) const;

	const AirportFTAClass *Airport() const
	{
		if (airport_tile == INVALID_TILE) return GetAirport(AT_DUMMY);
		return GetAirport(airport_type);
	}

	TileIndex xy;
	RoadStop *bus_stops;
	RoadStop *truck_stops;
	TileIndex train_tile;
	TileIndex airport_tile;
	TileIndex dock_tile;
	Town *town;

	/* Place to get a name from, in order of importance: */
	char *name;             ///< Custom name
	IndustryType indtype;   ///< Industry type to get the name from
	StringID string_id;     ///< Default name (town area) of station

	ViewportSign sign;

	uint16 had_vehicle_of_type;

	byte time_since_load;
	byte time_since_unload;
	byte delete_ctr;
	OwnerByte owner;
	byte facilities;
	byte airport_type;

	/* trainstation width/height */
	byte trainst_w, trainst_h;

	/** List of custom stations (StationSpecs) allocated to the station */
	uint8 num_specs;
	StationSpecList *speclist;

	Date build_date;  ///< Date of construction

	uint64 airport_flags;   ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32

	byte last_vehicle_type;
	std::list<Vehicle *> loading_vehicles;
	GoodsEntry goods[NUM_CARGO];  ///< Goods at this station

	uint16 random_bits;
	byte waiting_triggers;
	uint8 cached_anim_triggers; ///< Combined animation trigger bitmask, used to determine if trigger processing should happen.

	StationRect rect; ///< Station spread out rectangle (not saved) maintained by StationRect_xxx() functions

	static const int cDebugCtorLevel = 5;

	Station(TileIndex tile = INVALID_TILE);
	~Station();

	void AddFacility(byte new_facility_bit, TileIndex facil_xy);

	/**
	 * Mark the sign of a station dirty for repaint.
	 *
	 * @ingroup dirty
	 */
	void MarkDirty() const;

	/**
	 * Marks the tiles of the station as dirty.
	 *
	 * @ingroup dirty
	 */
	void MarkTilesDirty(bool cargo_change) const;
	bool TileBelongsToRailStation(TileIndex tile) const;
	uint GetPlatformLength(TileIndex tile, DiagDirection dir) const;
	uint GetPlatformLength(TileIndex tile) const;
	bool IsBuoy() const;

	uint GetCatchmentRadius() const;
};

#define FOR_ALL_STATIONS_FROM(var, start) FOR_ALL_ITEMS_FROM(Station, station_index, var, start)
#define FOR_ALL_STATIONS(var) FOR_ALL_STATIONS_FROM(var, 0)


/* Stuff for ROADSTOPS */

#define FOR_ALL_ROADSTOPS_FROM(var, start) FOR_ALL_ITEMS_FROM(RoadStop, roadstop_index, var, start)
#define FOR_ALL_ROADSTOPS(var) FOR_ALL_ROADSTOPS_FROM(var, 0)

/* End of stuff for ROADSTOPS */

#endif /* STATION_BASE_H */
