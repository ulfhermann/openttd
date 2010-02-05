/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file src/roadveh.h Road vehicle states */

#ifndef ROADVEH_H
#define ROADVEH_H

#include "vehicle_base.h"
#include "road_type.h"

struct RoadVehicle;

/** Road vehicle states */
enum RoadVehicleStates {
	/*
	 * Lower 4 bits are used for vehicle track direction. (Trackdirs)
	 * When in a road stop (bit 5 or bit 6 set) these bits give the
	 * track direction of the entry to the road stop.
	 * As the entry direction will always be a diagonal
	 * direction (X_NE, Y_SE, X_SW or Y_NW) only bits 0 and 3
	 * are needed to hold this direction. Bit 1 is then used to show
	 * that the vehicle is using the second road stop bay.
	 * Bit 2 is then used for drive-through stops to show the vehicle
	 * is stopping at this road stop.
	 */

	/* Numeric values */
	RVSB_IN_DEPOT                = 0xFE,                      ///< The vehicle is in a depot
	RVSB_WORMHOLE                = 0xFF,                      ///< The vehicle is in a tunnel and/or bridge

	/* Bit numbers */
	RVS_USING_SECOND_BAY         =    1,                      ///< Only used while in a road stop
	RVS_DRIVE_SIDE               =    4,                      ///< Only used when retrieving move data
	RVS_IN_ROAD_STOP             =    5,                      ///< The vehicle is in a road stop
	RVS_IN_DT_ROAD_STOP          =    6,                      ///< The vehicle is in a drive-through road stop

	/* Bit sets of the above specified bits */
	RVSB_IN_ROAD_STOP            = 1 << RVS_IN_ROAD_STOP,     ///< The vehicle is in a road stop
	RVSB_IN_ROAD_STOP_END        = RVSB_IN_ROAD_STOP + TRACKDIR_END,
	RVSB_IN_DT_ROAD_STOP         = 1 << RVS_IN_DT_ROAD_STOP,  ///< The vehicle is in a drive-through road stop
	RVSB_IN_DT_ROAD_STOP_END     = RVSB_IN_DT_ROAD_STOP + TRACKDIR_END,

	RVSB_TRACKDIR_MASK           = 0x0F,                      ///< The mask used to extract track dirs
	RVSB_ROAD_STOP_TRACKDIR_MASK = 0x09                       ///< Only bits 0 and 3 are used to encode the trackdir for road stops
};

/** State information about the Road Vehicle controller */
enum {
	RDE_NEXT_TILE = 0x80, ///< We should enter the next tile
	RDE_TURNED    = 0x40, ///< We just finished turning

	/* Start frames for when a vehicle enters a tile/changes its state.
	 * The start frame is different for vehicles that turned around or
	 * are leaving the depot as the do not start at the edge of the tile.
	 * For trams there are a few different start frames as there are two
	 * places where trams can turn. */
	RVC_DEFAULT_START_FRAME                =  0,
	RVC_TURN_AROUND_START_FRAME            =  1,
	RVC_DEPOT_START_FRAME                  =  6,
	RVC_START_FRAME_AFTER_LONG_TRAM        = 21,
	RVC_TURN_AROUND_START_FRAME_SHORT_TRAM = 16,
	/* Stop frame for a vehicle in a drive-through stop */
	RVC_DRIVE_THROUGH_STOP_FRAME           = 11,
	RVC_DEPOT_STOP_FRAME                   = 11,
};

enum RoadVehicleSubType {
	RVST_FRONT,
	RVST_ARTIC_PART,
};


void RoadVehUpdateCache(RoadVehicle *v);

/** Cached oftenly queried (NewGRF) values */
struct RoadVehicleCache {
	uint16 cached_total_length; ///< Length of the whole train, valid only for first engine.
	byte cached_veh_length;     ///< length of this vehicle in units of 1/8 of normal length, cached because this can be set by a callback
	EngineID first_engine;      ///< Cached EngineID of the front vehicle. INVALID_ENGINE for the front vehicle itself.
};

/**
 * Buses, trucks and trams belong to this class.
 */
struct RoadVehicle : public SpecializedVehicle<RoadVehicle, VEH_ROAD> {
	RoadVehicleCache rcache; ///< Cache of often used calculated values
	byte state;             ///< @see RoadVehicleStates
	byte frame;
	uint16 blocked_ctr;
	byte overtaking;
	byte overtaking_ctr;
	uint16 crashed_ctr;
	byte reverse_ctr;

	RoadType roadtype;
	RoadTypes compatible_roadtypes;

	/** We don't want GCC to zero our struct! It already is zeroed and has an index! */
	RoadVehicle() : SpecializedVehicle<RoadVehicle, VEH_ROAD>() {}
	/** We want to 'destruct' the right class. */
	virtual ~RoadVehicle() { this->PreDestructor(); }

	const char *GetTypeString() const { return "road vehicle"; }
	void MarkDirty();
	void UpdateDeltaXY(Direction direction);
	ExpensesType GetExpenseType(bool income) const { return income ? EXPENSES_ROADVEH_INC : EXPENSES_ROADVEH_RUN; }
	bool IsPrimaryVehicle() const { return this->IsRoadVehFront(); }
	SpriteID GetImage(Direction direction) const;
	int GetDisplaySpeed() const { return this->cur_speed / 2; }
	int GetDisplayMaxSpeed() const { return this->max_speed / 2; }
	Money GetRunningCost() const;
	int GetDisplayImageWidth(Point *offset = NULL) const;
	bool IsInDepot() const { return this->state == RVSB_IN_DEPOT; }
	bool IsStoppedInDepot() const;
	bool Tick();
	void OnNewDay();
	uint Crash(bool flooded = false);
	Trackdir GetVehicleTrackdir() const;
	TileIndex GetOrderStationLocation(StationID station);
	bool FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse);

	bool IsBus() const;

	/**
	 * Check if vehicle is a front engine
	 * @return Returns true if vehicle is a front engine
	 */
	FORCEINLINE bool IsRoadVehFront() const { return this->subtype == RVST_FRONT; }

	/**
	 * Set front engine state
	 */
	FORCEINLINE void SetRoadVehFront() { this->subtype = RVST_FRONT; }

	/**
	 * Check if vehicl is an articulated part of an engine
	 * @return Returns true if vehicle is an articulated part
	 */
	FORCEINLINE bool IsArticulatedPart() const { return this->subtype == RVST_ARTIC_PART; }

	/**
	 * Set a vehicle to be an articulated part
	 */
	FORCEINLINE void SetArticulatedPart() { this->subtype = RVST_ARTIC_PART; }

	/**
	 * Check if an engine has an articulated part.
	 * @return True if the engine has an articulated part.
	 */
	FORCEINLINE bool HasArticulatedPart() const { return this->Next() != NULL && this->Next()->IsArticulatedPart(); }
};

#define FOR_ALL_ROADVEHICLES(var) FOR_ALL_VEHICLES_OF_TYPE(RoadVehicle, var)

#endif /* ROADVEH_H */
