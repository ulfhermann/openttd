/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_func.h Functions related to vehicles. */

#ifndef VEHICLE_FUNC_H
#define VEHICLE_FUNC_H

#include "gfx_type.h"
#include "direction_type.h"
#include "command_type.h"
#include "vehicle_type.h"
#include "engine_type.h"
#include "transport_type.h"
#include "newgrf_config.h"
#include "company_type.h"

#define is_custom_sprite(x) (x >= 0xFD)
#define IS_CUSTOM_FIRSTHEAD_SPRITE(x) (x == 0xFD)
#define IS_CUSTOM_SECONDHEAD_SPRITE(x) (x == 0xFE)

typedef Vehicle *VehicleFromPosProc(Vehicle *v, void *data);

void VehicleServiceInDepot(Vehicle *v);
uint CountVehiclesInChain(const Vehicle *v);
void FindVehicleOnPos(TileIndex tile, void *data, VehicleFromPosProc *proc);
void FindVehicleOnPosXY(int x, int y, void *data, VehicleFromPosProc *proc);
bool HasVehicleOnPos(TileIndex tile, void *data, VehicleFromPosProc *proc);
bool HasVehicleOnPosXY(int x, int y, void *data, VehicleFromPosProc *proc);
void CallVehicleTicks();
uint8 CalcPercentVehicleFilled(const Vehicle *v, StringID *colour);

byte VehicleRandomBits();
void ResetVehiclePosHash();
void ResetVehicleColourMap();

byte GetBestFittingSubType(Vehicle *v_from, Vehicle *v_for);
CommandCost RefitVehicle(Vehicle *v, bool only_this, CargoID new_cid, byte new_subtype, DoCommandFlag flags);

void ViewportAddVehicles(DrawPixelInfo *dpi);

void ShowNewGrfVehicleError(EngineID engine, StringID part1, StringID part2, GRFBugs bug_type, bool critical);
CommandCost TunnelBridgeIsFree(TileIndex tile, TileIndex endtile, const Vehicle *ignore = NULL);

void DecreaseVehicleValue(Vehicle *v);
void CheckVehicleBreakdown(Vehicle *v);
void AgeVehicle(Vehicle *v);
void VehicleEnteredDepotThisTick(Vehicle *v);

void VehicleMove(Vehicle *v, bool update_viewport);
void MarkSingleVehicleDirty(const Vehicle *v);

UnitID GetFreeUnitNumber(VehicleType type);

CommandCost SendAllVehiclesToDepot(VehicleType type, DoCommandFlag flags, bool service, Owner owner, uint16 vlw_flag, uint32 id);
void VehicleEnterDepot(Vehicle *v);

bool CanBuildVehicleInfrastructure(VehicleType type);

/** Position information of a vehicle after it moved */
struct GetNewVehiclePosResult {
	int x, y;  ///< x and y position of the vehicle after moving
	TileIndex old_tile; ///< Current tile of the vehicle
	TileIndex new_tile; ///< Tile of the vehicle after moving
};

GetNewVehiclePosResult GetNewVehiclePos(const Vehicle *v);
Direction GetDirectionTowards(const Vehicle *v, int x, int y);

static inline bool IsCompanyBuildableVehicleType(VehicleType type)
{
	switch (type) {
		case VEH_TRAIN:
		case VEH_ROAD:
		case VEH_SHIP:
		case VEH_AIRCRAFT:
			return true;

		default: return false;
	}
}

static inline bool IsCompanyBuildableVehicleType(const BaseVehicle *v)
{
	return IsCompanyBuildableVehicleType(v->type);
}

const struct Livery *GetEngineLivery(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v);

/**
 * Get the colour map for an engine. This used for unbuilt engines in the user interface.
 * @param engine_type ID of engine
 * @param company ID of company
 * @return A ready-to-use palette modifier
 */
SpriteID GetEnginePalette(EngineID engine_type, CompanyID company);

/**
 * Get the colour map for a vehicle.
 * @param v Vehicle to get colour map for
 * @return A ready-to-use palette modifier
 */
SpriteID GetVehiclePalette(const Vehicle *v);

uint GetVehicleCapacity(const Vehicle *v, uint16 *mail_capacity = NULL);

extern const uint32 _veh_build_proc_table[];
extern const uint32 _veh_sell_proc_table[];
extern const uint32 _veh_refit_proc_table[];
extern const uint32 _send_to_depot_proc_table[];

/* Functions to find the right command for certain vehicle type */
static inline uint32 GetCmdBuildVeh(VehicleType type)
{
	return _veh_build_proc_table[type];
}

static inline uint32 GetCmdBuildVeh(const BaseVehicle *v)
{
	return GetCmdBuildVeh(v->type);
}

static inline uint32 GetCmdSellVeh(VehicleType type)
{
	return _veh_sell_proc_table[type];
}

static inline uint32 GetCmdSellVeh(const BaseVehicle *v)
{
	return GetCmdSellVeh(v->type);
}

static inline uint32 GetCmdRefitVeh(VehicleType type)
{
	return _veh_refit_proc_table[type];
}

static inline uint32 GetCmdRefitVeh(const BaseVehicle *v)
{
	return GetCmdRefitVeh(v->type);
}

static inline uint32 GetCmdSendToDepot(VehicleType type)
{
	return _send_to_depot_proc_table[type];
}

static inline uint32 GetCmdSendToDepot(const BaseVehicle *v)
{
	return GetCmdSendToDepot(v->type);
}

bool EnsureNoVehicleOnGround(TileIndex tile);
void StopAllVehicles();

extern VehicleID _vehicle_id_ctr_day;
extern const Vehicle *_place_clicked_vehicle;
extern VehicleID _new_vehicle_id;
extern uint16 _returned_refit_capacity;
extern byte _age_cargo_skip_counter;

bool CanVehicleUseStation(EngineID engine_type, const struct Station *st);
bool CanVehicleUseStation(const Vehicle *v, const struct Station *st);

void ReleaseDisastersTargetingVehicle(VehicleID vehicle);

#endif /* VEHICLE_FUNC_H */
