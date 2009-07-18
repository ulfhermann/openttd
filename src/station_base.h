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
#include "station_map.h"
#include <list>
#include <map>
#include <set>

typedef Pool<Station, StationID, 32, 64000> StationPool;
extern StationPool _station_pool;

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

	inline LinkStat & operator+=(const LinkStat & other)
	{
		this->capacity += other.capacity;
		this->usage += other.usage;
		this->frozen += other.frozen;
		return *this;
	}

	inline void Clear()
	{
		this->capacity = 0;
		this->usage = 0;
		this->frozen = 0;
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

	inline FlowStat & operator*=(uint factor) {
		planned *= factor;
		sent *= factor;
		return *this;
	}

	inline FlowStat & operator/=(uint divident) {
		planned /= divident;
		sent /= divident;
		return *this;
	}

	inline FlowStat & operator+=(const FlowStat & other)
	{
		assert(this->via == INVALID_STATION || other.via == INVALID_STATION || this->via == other.via);
		this->via = other.via;
		this->planned += other.planned;
		this->sent += other.sent;
		return *this;
	}

	inline void Clear()
	{
		this->planned = 0;
		this->sent = 0;
		this->via = INVALID_STATION;
	}
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
		last_age(255),
		last_component(0)
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
	
	FlowStat GetSumFlowVia(StationID via) const;

	/**
	 * update the flow stats for count cargo from source sent to next
	 */
	void UpdateFlowStats(StationID source, uint count, StationID next);

	/**
	 * update the flow stats for count cargo that cannot be delivered here
	 * return the direction where it is sent
	 */
	StationID UpdateFlowStatsTransfer(StationID source, uint count, StationID curr);
private:
	void UpdateFlowStats(FlowStatSet & flow_stats, FlowStatSet::iterator flow_it, uint count);
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

/** Base class for all station-ish types */
struct BaseStation {
	TileIndex xy;                   ///< Base tile of the station
	ViewportSign sign;              ///< NOSAVE: Dimensions of sign
	byte delete_ctr;                ///< Delete counter. If greater than 0 then it is decremented until it reaches 0; the waypoint is then is deleted.

	char *name;                     ///< Custom name
	StringID string_id;             ///< Default name (town area) of station

	Town *town;                     ///< The town this station is associated with
	OwnerByte owner;                ///< The owner of this station
	StationFacilityByte facilities; ///< The facilities that this station has

	uint8 num_specs;                ///< NOSAVE: Number of specs in the speclist
	StationSpecList *speclist;      ///< NOSAVE: List of station specs of this station

	Date build_date;                ///< Date of construction

	uint16 random_bits;             ///< Random bits assigned to this station
	byte waiting_triggers;          ///< Waiting triggers (NewGRF) for this station
	uint8 cached_anim_triggers;     ///< NOSAVE: Combined animation trigger bitmask, used to determine if trigger processing should happen.

	BaseStation(TileIndex tile = INVALID_TILE) : xy(tile) { }
	virtual ~BaseStation();

	/**
	 * Check whether a specific tile belongs to this station.
	 * @param tile the tile to check
	 * @return true if the tile belongs to this station
	 */
	virtual bool TileBelongsToRailStation(TileIndex tile) const = 0;

	/**
	 * Helper function to get a NewGRF variable that isn't implemented by the base class.
	 * @param object the resolver object related to this query
	 * @param variable that is queried
	 * @param parameter parameter for that variable
	 * @param available will return false if ever the variable asked for does not exist
	 * @return the value stored in the corresponding variable
	 */
	virtual uint32 GetNewGRFVariable(const struct ResolverObject *object, byte variable, byte parameter, bool *available) const = 0;

	/**
	 * Update the coordinated of the sign (as shown in the viewport).
	 */
	virtual void UpdateVirtCoord() = 0;

	/**
	 * Get the base station belonging to a specific tile.
	 * @param tile The tile to get the base station from.
	 * @return the station associated with that tile.
	 */
	static BaseStation *GetByTile(TileIndex tile);
};

typedef SmallVector<Industry *, 2> IndustryVector;

/** Station data structure */
struct Station : StationPool::PoolItem<&_station_pool>, BaseStation {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == ROADSTOP_BUS ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const struct RoadVehicle *v) const;

	const AirportFTAClass *Airport() const
	{
		if (airport_tile == INVALID_TILE) return GetAirport(AT_DUMMY);
		return GetAirport(airport_type);
	}

	RoadStop *bus_stops;
	RoadStop *truck_stops;
	TileIndex train_tile;
	TileIndex airport_tile;
	TileIndex dock_tile;

	IndustryType indtype;   ///< Industry type to get the name from

	StationHadVehicleOfTypeByte had_vehicle_of_type;

	byte time_since_load;
	byte time_since_unload;
	byte airport_type;

	/* trainstation width/height */
	byte trainst_w, trainst_h;

	uint64 airport_flags;   ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32

	byte last_vehicle_type;
	std::list<Vehicle *> loading_vehicles;
	GoodsEntry goods[NUM_CARGO];  ///< Goods at this station

	IndustryVector industries_near; ///< Cached list of industries near the station that can accept cargo, @see DeliverGoodsToIndustry()

	StationRect rect; ///< Station spread out rectangle (not saved) maintained by StationRect_xxx() functions

	Station(TileIndex tile = INVALID_TILE);
	~Station();

	void AddFacility(StationFacility new_facility_bit, TileIndex facil_xy);

	/**
	 * Marks the tiles of the station as dirty.
	 *
	 * @ingroup dirty
	 */
	void MarkTilesDirty(bool cargo_change) const;

	void UpdateVirtCoord();

	uint GetPlatformLength(TileIndex tile, DiagDirection dir) const;
	uint GetPlatformLength(TileIndex tile) const;
	void RecomputeIndustriesNear();
	static void RecomputeIndustriesNearForAll();

	uint GetCatchmentRadius() const;

	/* virtual */ FORCEINLINE bool TileBelongsToRailStation(TileIndex tile) const
	{
		return IsRailwayStationTile(tile) && GetStationIndex(tile) == this->index;
	}

	/* virtual */ uint32 GetNewGRFVariable(const ResolverObject *object, byte variable, byte parameter, bool *available) const;

	/**
	 * Determines whether a station is a buoy only.
	 * @todo Ditch this encoding of buoys
	 */
	FORCEINLINE bool IsBuoy() const
	{
		return (this->had_vehicle_of_type & HVOT_BUOY) != 0;
	}

	static FORCEINLINE Station *GetByTile(TileIndex tile)
	{
		return Station::Get(GetStationIndex(tile));
	}

	static void PostDestructor(size_t index);

	static FORCEINLINE Station *From(BaseStation *st)
	{
		return (Station *)st;
	}

	static FORCEINLINE const Station *From(const BaseStation *st)
	{
		return (const Station *)st;
	}
};

#define FOR_ALL_STATIONS_FROM(var, start) FOR_ALL_ITEMS_FROM(Station, station_index, var, start)
#define FOR_ALL_STATIONS(var) FOR_ALL_STATIONS_FROM(var, 0)

#endif /* STATION_BASE_H */
