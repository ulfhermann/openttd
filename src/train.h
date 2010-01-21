/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train.h Base for the train class. */

#ifndef TRAIN_H
#define TRAIN_H

#include "vehicle_base.h"
#include "newgrf_engine.h"
#include "cargotype.h"
#include "rail.h"
#include "engine_base.h"
#include "rail_map.h"

struct Train;

enum VehicleRailFlags {
	VRF_REVERSING         = 0,

	/* used to calculate if train is going up or down */
	VRF_GOINGUP           = 1,
	VRF_GOINGDOWN         = 2,

	/* used to store if a wagon is powered or not */
	VRF_POWEREDWAGON      = 3,

	/* used to reverse the visible direction of the vehicle */
	VRF_REVERSE_DIRECTION = 4,

	/* used to mark train as lost because PF can't find the route */
	VRF_NO_PATH_TO_DESTINATION = 5,

	/* used to mark that electric train engine is allowed to run on normal rail */
	VRF_EL_ENGINE_ALLOWED_NORMAL_RAIL = 6,

	/* used for vehicle var 0xFE bit 8 (toggled each time the train is reversed, accurate for first vehicle only) */
	VRF_TOGGLE_REVERSE = 7,

	/* used to mark a train that can't get a path reservation */
	VRF_TRAIN_STUCK    = 8,
};

byte FreightWagonMult(CargoID cargo);

void CheckTrainsLengths();

void FreeTrainTrackReservation(const Train *v, TileIndex origin = INVALID_TILE, Trackdir orig_td = INVALID_TRACKDIR);
bool TryPathReserve(Train *v, bool mark_as_stuck = false, bool first_tile_okay = false);

int GetTrainStopLocation(StationID station_id, TileIndex tile, const Train *v, int *station_ahead, int *station_length);

/** Variables that are cached to improve performance and such */
struct TrainCache {
	/* Cached wagon override spritegroup */
	const struct SpriteGroup *cached_override;

	uint16 last_speed; // NOSAVE: only used in UI

	/* cached values, recalculated on load and each time a vehicle is added to/removed from the consist. */
	uint32 cached_power;            ///< total power of the consist.
	uint16 cached_axle_resistance;  ///< Resistance caused by the axles of the vehicle
	uint32 cached_air_drag;         ///< Air drag coefficient of the vehicle
	uint16 cached_total_length;     ///< Length of the whole train, valid only for first engine.
	uint8 cached_veh_length;        ///< length of this vehicle in units of 1/8 of normal length, cached because this can be set by a callback
	bool cached_tilt;               ///< train can tilt; feature provides a bonus in curves

	/* cached values, recalculated when the cargo on a train changes (in addition to the conditions above) */
	uint32 cached_weight;           ///< total weight of the consist.
	uint32 cached_slope_resistance; ///< Resistance caused by weight when this vehicle part is at a slope
	uint32 cached_max_te;           ///< max tractive effort of consist

	/* cached max. speed / acceleration data */
	uint16 cached_max_speed;    ///< max speed of the consist. (minimum of the max speed of all vehicles in the consist)
	int cached_max_curve_speed; ///< max consist speed limited by curves

	/**
	 * Position/type of visual effect.
	 * bit 0 - 3 = position of effect relative to vehicle. (0 = front, 8 = centre, 15 = rear)
	 * bit 4 - 5 = type of effect. (0 = default for engine class, 1 = steam, 2 = diesel, 3 = electric)
	 * bit     6 = disable visual effect.
	 * bit     7 = disable powered wagons.
	 */
	byte cached_vis_effect;
	byte user_def_data;

	EngineID first_engine;  ///< cached EngineID of the front vehicle. INVALID_ENGINE for the front vehicle itself.
};

/** What is the status of our acceleration? */
enum AccelStatus {
	AS_ACCEL, ///< We want to go faster, if possible ofcourse
	AS_BRAKE  ///< We want to stop
};

/**
 * 'Train' is either a loco or a wagon.
 */
struct Train : public SpecializedVehicle<Train, VEH_TRAIN> {
	TrainCache tcache;

	/* Link between the two ends of a multiheaded engine */
	Train *other_multiheaded_part;

	uint16 crash_anim_pos;

	uint16 flags;
	TrackBitsByte track;
	byte force_proceed;
	RailTypeByte railtype;
	RailTypes compatible_railtypes;

	/** Ticks waiting in front of a signal, ticks being stuck or a counter for forced proceeding through signals. */
	uint16 wait_counter;

	/** We don't want GCC to zero our struct! It already is zeroed and has an index! */
	Train() : SpecializedVehicle<Train, VEH_TRAIN>() {}
	/** We want to 'destruct' the right class. */
	virtual ~Train() { this->PreDestructor(); }

	const char *GetTypeString() const { return "train"; }
	void MarkDirty();
	void UpdateDeltaXY(Direction direction);
	ExpensesType GetExpenseType(bool income) const { return income ? EXPENSES_TRAIN_INC : EXPENSES_TRAIN_RUN; }
	void PlayLeaveStationSound() const;
	bool IsPrimaryVehicle() const { return this->IsFrontEngine(); }
	SpriteID GetImage(Direction direction) const;
	int GetDisplaySpeed() const { return this->tcache.last_speed; }
	int GetDisplayMaxSpeed() const { return this->tcache.cached_max_speed; }
	Money GetRunningCost() const;
	int GetDisplayImageWidth(Point *offset = NULL) const;
	bool IsInDepot() const;
	bool IsStoppedInDepot() const;
	bool Tick();
	void OnNewDay();
	uint Crash(bool flooded = false);
	Trackdir GetVehicleTrackdir() const;
	TileIndex GetOrderStationLocation(StationID station);
	bool FindClosestDepot(TileIndex *location, DestinationID *destination, bool *reverse);

	void ReserveTrackUnderConsist() const;

	int GetCurveSpeedLimit() const;

	void ConsistChanged(bool same_length);
	void CargoChanged();
	void PowerChanged();

	int UpdateSpeed();

	void UpdateAcceleration();

	int GetCurrentMaxSpeed() const;
	int GetAcceleration() const;

	/**
	 * enum to handle train subtypes
	 * Do not access it directly unless you have to. Use the access functions below
	 * This is an enum to tell what bit to access as it is a bitmask
	 */
	enum TrainSubtype {
		TS_FRONT             = 0, ///< Leading engine of a train
		TS_ARTICULATED_PART  = 1, ///< Articulated part of an engine
		TS_WAGON             = 2, ///< Wagon
		TS_ENGINE            = 3, ///< Engine, that can be front engine, but might be placed behind another engine
		TS_FREE_WAGON        = 4, ///< First in a wagon chain (in depot)
		TS_MULTIHEADED       = 5, ///< Engine is multiheaded
	};

	/**
	 * Set front engine state
	 */
	FORCEINLINE void SetFrontEngine() { SetBit(this->subtype, TS_FRONT); }

	/**
	 * Remove the front engine state
	 */
	FORCEINLINE void ClearFrontEngine() { ClrBit(this->subtype, TS_FRONT); }

	/**
	 * Set a vehicle to be an articulated part
	 */
	FORCEINLINE void SetArticulatedPart() { SetBit(this->subtype, TS_ARTICULATED_PART); }

	/**
	 * Clear a vehicle from being an articulated part
	 */
	FORCEINLINE void ClearArticulatedPart() { ClrBit(this->subtype, TS_ARTICULATED_PART); }

	/**
	 * Set a vehicle to be a wagon
	 */
	FORCEINLINE void SetWagon() { SetBit(this->subtype, TS_WAGON); }

	/**
	 * Clear wagon property
	 */
	FORCEINLINE void ClearWagon() { ClrBit(this->subtype, TS_WAGON); }

	/**
	 * Set engine status
	 */
	FORCEINLINE void SetEngine() { SetBit(this->subtype, TS_ENGINE); }

	/**
	 * Clear engine status
	 */
	FORCEINLINE void ClearEngine() { ClrBit(this->subtype, TS_ENGINE); }

	/**
	 * Set if a vehicle is a free wagon
	 */
	FORCEINLINE void SetFreeWagon() { SetBit(this->subtype, TS_FREE_WAGON); }

	/**
	 * Clear a vehicle from being a free wagon
	 */
	FORCEINLINE void ClearFreeWagon() { ClrBit(this->subtype, TS_FREE_WAGON); }

	/**
	 * Set if a vehicle is a multiheaded engine
	 */
	FORCEINLINE void SetMultiheaded() { SetBit(this->subtype, TS_MULTIHEADED); }

	/**
	 * Clear multiheaded engine property
	 */
	FORCEINLINE void ClearMultiheaded() { ClrBit(this->subtype, TS_MULTIHEADED); }


	/**
	 * Check if train is a front engine
	 * @return Returns true if train is a front engine
	 */
	FORCEINLINE bool IsFrontEngine() const { return HasBit(this->subtype, TS_FRONT); }

	/**
	 * Check if train is a free wagon (got no engine in front of it)
	 * @return Returns true if train is a free wagon
	 */
	FORCEINLINE bool IsFreeWagon() const { return HasBit(this->subtype, TS_FREE_WAGON); }

	/**
	 * Check if a vehicle is an engine (can be first in a train)
	 * @return Returns true if vehicle is an engine
	 */
	FORCEINLINE bool IsEngine() const { return HasBit(this->subtype, TS_ENGINE); }

	/**
	 * Check if a train is a wagon
	 * @return Returns true if vehicle is a wagon
	 */
	FORCEINLINE bool IsWagon() const { return HasBit(this->subtype, TS_WAGON); }

	/**
	 * Check if train is a multiheaded engine
	 * @return Returns true if vehicle is a multiheaded engine
	 */
	FORCEINLINE bool IsMultiheaded() const { return HasBit(this->subtype, TS_MULTIHEADED); }

	/**
	 * Tell if we are dealing with the rear end of a multiheaded engine.
	 * @return True if the engine is the rear part of a dualheaded engine.
	 */
	FORCEINLINE bool IsRearDualheaded() const { return this->IsMultiheaded() && !this->IsEngine(); }

	/**
	 * Check if train is an articulated part of an engine
	 * @return Returns true if train is an articulated part
	 */
	FORCEINLINE bool IsArticulatedPart() const { return HasBit(this->subtype, TS_ARTICULATED_PART); }

	/**
	 * Check if an engine has an articulated part.
	 * @return True if the engine has an articulated part.
	 */
	FORCEINLINE bool HasArticulatedPart() const { return this->Next() != NULL && this->Next()->IsArticulatedPart(); }


	/**
	 * Get the next part of a multi-part engine.
	 * Will only work on a multi-part engine (this->EngineHasArticPart() == true),
	 * Result is undefined for normal engine.
	 * @return next part of articulated engine
	 */
	FORCEINLINE Train *GetNextArticPart() const
	{
		assert(this->HasArticulatedPart());
		return this->Next();
	}

	/**
	 * Get the first part of a multi-part engine.
	 * @return First part of the engine.
	 */
	FORCEINLINE Train *GetFirstEnginePart()
	{
		Train *v = this;
		while (v->IsArticulatedPart()) v = v->Previous();
		return v;
	}

	/**
	 * Get the first part of a multi-part engine.
	 * @return First part of the engine.
	 */
	FORCEINLINE const Train *GetFirstEnginePart() const
	{
		const Train *v = this;
		while (v->IsArticulatedPart()) v = v->Previous();
		return v;
	}

	/**
	 * Get the last part of a multi-part engine.
	 * @return Last part of the engine.
	 */
	FORCEINLINE Train *GetLastEnginePart()
	{
		Train *v = this;
		while (v->HasArticulatedPart()) v = v->GetNextArticPart();
		return v;
	}

	/**
	 * Get the next real (non-articulated part) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	FORCEINLINE Train *GetNextVehicle() const
	{
		const Train *v = this;
		while (v->HasArticulatedPart()) v = v->GetNextArticPart();

		/* v now contains the last artic part in the engine */
		return v->Next();
	}

	/**
	 * Get the previous real (non-articulated part) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	FORCEINLINE Train *GetPrevVehicle() const
	{
		Train *v = this->Previous();
		while (v != NULL && v->IsArticulatedPart()) v = v->Previous();

		return v;
	}

	/**
	 * Get the next real (non-articulated part and non rear part of dualheaded engine) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	FORCEINLINE Train *GetNextUnit() const
	{
		Train *v = this->GetNextVehicle();
		if (v != NULL && v->IsRearDualheaded()) v = v->GetNextVehicle();

		return v;
	}

	/**
	 * Get the previous real (non-articulated part and non rear part of dualheaded engine) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	FORCEINLINE Train *GetPrevUnit()
	{
		Train *v = this->GetPrevVehicle();
		if (v != NULL && v->IsRearDualheaded()) v = v->GetPrevVehicle();

		return v;
	}


protected: /* These functions should not be called outside acceleration code. */

	/**
	 * Allows to know the power value that this vehicle will use.
	 * @return Power value from the engine in HP, or zero if the vehicle is not powered.
	 */
	FORCEINLINE uint16 GetPower() const
	{
		/* Power is not added for articulated parts */
		if (!this->IsArticulatedPart() && HasPowerOnRail(this->railtype, GetRailType(this->tile))) {
			uint16 power = GetVehicleProperty(this, PROP_TRAIN_POWER, RailVehInfo(this->engine_type)->power);
			/* Halve power for multiheaded parts */
			if (this->IsMultiheaded()) power /= 2;
			return power;
		}

		return 0;
	}

	/**
	 * Returns a value if this articulated part is powered.
	 * @return Power value from the articulated part in HP, or zero if it is not powered.
	 */
	FORCEINLINE uint16 GetPoweredPartPower(const Train *head) const
	{
		if (HasBit(this->flags, VRF_POWEREDWAGON) && HasPowerOnRail(head->railtype, GetRailType(head->tile))) {
			return RailVehInfo(this->tcache.first_engine)->pow_wag_power;
		}

		return 0;
	}

	/**
	 * Allows to know the weight value that this vehicle will use.
	 * @return Weight value from the engine in tonnes.
	 */
	FORCEINLINE uint16 GetWeight() const
	{
		uint16 weight = (CargoSpec::Get(this->cargo_type)->weight * this->cargo.Count() * FreightWagonMult(this->cargo_type)) / 16;

		/* Vehicle weight is not added for articulated parts. */
		if (!this->IsArticulatedPart()) {
			weight += GetVehicleProperty(this, PROP_TRAIN_WEIGHT, RailVehInfo(this->engine_type)->weight);
		}

		/* Powered wagons have extra weight added. */
		if (HasBit(this->flags, VRF_POWEREDWAGON)) {
			weight += RailVehInfo(this->tcache.first_engine)->pow_wag_weight;
		}

		return weight;
	}

	/**
	 * Allows to know the tractive effort value that this vehicle will use.
	 * @return Tractive effort value from the engine.
	 */
	FORCEINLINE byte GetTractiveEffort() const
	{
		return GetVehicleProperty(this, PROP_TRAIN_TRACTIVE_EFFORT, RailVehInfo(this->engine_type)->tractive_effort);
	}

	/**
	 * Checks the current acceleration status of this vehicle.
	 * @return Acceleration status.
	 */
	FORCEINLINE AccelStatus GetAccelerationStatus() const
	{
		return (this->vehstatus & VS_STOPPED) || HasBit(this->flags, VRF_REVERSING) || HasBit(this->flags, VRF_TRAIN_STUCK) ? AS_BRAKE : AS_ACCEL;
	}

	/**
	 * Calculates the current speed of this vehicle.
	 * @return Current speed in mph.
	 */
	FORCEINLINE uint16 GetCurrentSpeed() const
	{
		return this->cur_speed * 10 / 16;
	}

	/**
	 * Returns the rolling friction coefficient of this vehicle.
	 * @return Rolling friction coefficient in [1e-3].
	 */
	FORCEINLINE uint32 GetRollingFriction() const
	{
		return 35;
	}

	/**
	 * Calculates the total slope resistance for this vehicle.
	 * @return Slope resistance.
	 */
	FORCEINLINE int32 GetSlopeResistance() const
	{
		int32 incl = 0;

		for (const Train *u = this; u != NULL; u = u->Next()) {
			if (HasBit(u->flags, VRF_GOINGUP)) {
				incl += u->tcache.cached_slope_resistance;
			} else if (HasBit(u->flags, VRF_GOINGDOWN)) {
				incl -= u->tcache.cached_slope_resistance;
			}
		}

		return incl;
	}

	/**
	 * Allows to know the acceleration type of a vehicle.
	 * @return Acceleration type of the vehicle.
	 */
	FORCEINLINE int GetAccelerationType() const
	{
		return GetRailTypeInfo(this->railtype)->acceleration_type;
	}
};

#define FOR_ALL_TRAINS(var) FOR_ALL_VEHICLES_OF_TYPE(Train, var)

#endif /* TRAIN_H */
