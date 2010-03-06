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

#include "road_type.h"
#include "ground_vehicle.hpp"
#include "engine_base.h"
#include "cargotype.h"
#include "road_map.h"

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
struct RoadVehicle : public GroundVehicle<RoadVehicle, VEH_ROAD> {
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
	RoadVehicle() : GroundVehicle<RoadVehicle, VEH_ROAD>() {}
	/** We want to 'destruct' the right class. */
	virtual ~RoadVehicle() { this->PreDestructor(); }

	friend struct GroundVehicle<RoadVehicle, VEH_ROAD>; // GroundVehicle needs to use the acceleration functions defined at RoadVehicle.

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

	int GetCurrentMaxSpeed() const;

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

protected: // These functions should not be called outside acceleration code.

	/**
	 * Allows to know the power value that this vehicle will use.
	 * @return Power value from the engine in HP, or zero if the vehicle is not powered.
	 */
	FORCEINLINE uint16 GetPower() const
	{
		/* Power is not added for articulated parts */
		if (!this->IsArticulatedPart()) {
			return 10 * RoadVehInfo(this->engine_type)->power; // Road vehicle power is in units of 10 HP.
		}
		return 0;
	}

	/**
	 * Returns a value if this articulated part is powered.
	 * @return Zero, because road vehicles don't have powered parts.
	 */
	FORCEINLINE uint16 GetPoweredPartPower(const RoadVehicle *head) const
	{
		return 0;
	}

	/**
	 * Allows to know the weight value that this vehicle will use.
	 * @return Weight value from the engine in tonnes.
	 */
	FORCEINLINE uint16 GetWeight() const
	{
		uint16 weight = (CargoSpec::Get(this->cargo_type)->weight * this->cargo.Count()) / 16;

		/* Vehicle weight is not added for articulated parts. */
		if (!this->IsArticulatedPart()) {
			weight += RoadVehInfo(this->engine_type)->weight / 4; // Road vehicle weight is in units of 1/4 t.
		}

		return weight;
	}

	/**
	 * Allows to know the tractive effort value that this vehicle will use.
	 * @return Tractive effort value from the engine.
	 */
	FORCEINLINE byte GetTractiveEffort() const
	{
		return RoadVehInfo(this->engine_type)->tractive_effort;
	}

	/**
	 * Checks the current acceleration status of this vehicle.
	 * @return Acceleration status.
	 */
	FORCEINLINE AccelStatus GetAccelerationStatus() const
	{
		return (this->vehstatus & VS_STOPPED) ? AS_BRAKE : AS_ACCEL;
	}

	/**
	 * Calculates the current speed of this vehicle.
	 * @return Current speed in mph.
	 */
	FORCEINLINE uint16 GetCurrentSpeed() const
	{
		return this->cur_speed * 10 / 32;
	}

	/**
	 * Returns the rolling friction coefficient of this vehicle.
	 * @return Rolling friction coefficient in [1e-3].
	 */
	FORCEINLINE uint32 GetRollingFriction() const
	{
		/* Trams have a slightly greater friction coefficient than trains. The rest of road vehicles have bigger values. */
		return (this->roadtype == ROADTYPE_TRAM) ? 50 : 75;
	}

	/**
	 * Allows to know the acceleration type of a vehicle.
	 * @return Zero, road vehicles always use a normal acceleration method.
	 */
	FORCEINLINE int GetAccelerationType() const
	{
		return 0;
	}

	/**
	 * Returns the slope steepness used by this vehicle.
	 * @return Slope steepness used by the vehicle.
	 */
	FORCEINLINE uint32 GetSlopeSteepness() const
	{
		return 20 * _settings_game.vehicle.roadveh_slope_steepness; // 1% slope * slope steepness
	}

	/**
	 * Gets the maximum speed of the vehicle, ignoring the limitations of the kind of track the vehicle is on.
	 * @return Maximum speed of the vehicle.
	 */
	FORCEINLINE uint16 GetInitialMaxSpeed() const
	{
		return this->max_speed;
	}

	/**
	 * Gets the maximum speed allowed by the track for this vehicle.
	 * @return Since roads don't limit road vehicle speed, it returns always zero.
	 */
	FORCEINLINE uint16 GetMaxTrackSpeed() const
	{
		return 0;
	}

	/**
	 * Checks if the vehicle is at a tile that can be sloped.
	 * @return True if the tile can be sloped.
	 */
	FORCEINLINE bool TileMayHaveSlopedTrack() const
	{
		TrackStatus ts = GetTileTrackStatus(this->tile, TRANSPORT_ROAD, this->compatible_roadtypes);
		TrackBits trackbits = TrackStatusToTrackBits(ts);

		return trackbits == TRACK_BIT_X || trackbits == TRACK_BIT_Y;
	}
};

#define FOR_ALL_ROADVEHICLES(var) FOR_ALL_VEHICLES_OF_TYPE(RoadVehicle, var)

#endif /* ROADVEH_H */
