/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_cmd.cpp Commands for vehicles. */

#include "stdafx.h"
#include "roadveh.h"
#include "news_func.h"
#include "airport.h"
#include "command_func.h"
#include "company_func.h"
#include "vehicle_gui.h"
#include "train.h"
#include "aircraft.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "functions.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "depot_map.h"
#include "vehiclelist.h"
#include "engine_base.h"

#include "table/strings.h"

/* Tables used in vehicle.h to find the right command for a certain vehicle type */
const uint32 _veh_build_proc_table[] = {
	CMD_BUILD_RAIL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_TRAIN),
	CMD_BUILD_ROAD_VEH     | CMD_MSG(STR_ERROR_CAN_T_BUY_ROAD_VEHICLE),
	CMD_BUILD_SHIP         | CMD_MSG(STR_ERROR_CAN_T_BUY_SHIP),
	CMD_BUILD_AIRCRAFT     | CMD_MSG(STR_ERROR_CAN_T_BUY_AIRCRAFT),
};

const uint32 _veh_sell_proc_table[] = {
	CMD_SELL_RAIL_WAGON | CMD_MSG(STR_ERROR_CAN_T_SELL_TRAIN),
	CMD_SELL_ROAD_VEH   | CMD_MSG(STR_ERROR_CAN_T_SELL_ROAD_VEHICLE),
	CMD_SELL_SHIP       | CMD_MSG(STR_ERROR_CAN_T_SELL_SHIP),
	CMD_SELL_AIRCRAFT   | CMD_MSG(STR_ERROR_CAN_T_SELL_AIRCRAFT),
};

const uint32 _veh_refit_proc_table[] = {
	CMD_REFIT_RAIL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_REFIT_TRAIN),
	CMD_REFIT_ROAD_VEH     | CMD_MSG(STR_ERROR_CAN_T_REFIT_ROAD_VEHICLE),
	CMD_REFIT_SHIP         | CMD_MSG(STR_ERROR_CAN_T_REFIT_SHIP),
	CMD_REFIT_AIRCRAFT     | CMD_MSG(STR_ERROR_CAN_T_REFIT_AIRCRAFT),
};

const uint32 _send_to_depot_proc_table[] = {
	/* TrainGotoDepot has a nice randomizer in the pathfinder, which causes desyncs... */
	CMD_SEND_TRAIN_TO_DEPOT     | CMD_MSG(STR_ERROR_CAN_T_SEND_TRAIN_TO_DEPOT) | CMD_NO_TEST_IF_IN_NETWORK,
	CMD_SEND_ROADVEH_TO_DEPOT   | CMD_MSG(STR_ERROR_CAN_T_SEND_ROAD_VEHICLE_TO_DEPOT),
	CMD_SEND_SHIP_TO_DEPOT      | CMD_MSG(STR_ERROR_CAN_T_SEND_SHIP_TO_DEPOT),
	CMD_SEND_AIRCRAFT_TO_HANGAR | CMD_MSG(STR_ERROR_CAN_T_SEND_AIRCRAFT_TO_HANGAR),
};

/** Start/Stop a vehicle
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle to start/stop
 * @param p2 bit 0: Shall the start/stop newgrf callback be evaluated (only valid with DC_AUTOREPLACE for network safety)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdStartStopVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	/* Disable the effect of p2 bit 0, when DC_AUTOREPLACE is not set */
	if ((flags & DC_AUTOREPLACE) == 0) SetBit(p2, 0);

	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	switch (v->type) {
		case VEH_TRAIN:
			if ((v->vehstatus & VS_STOPPED) && Train::From(v)->acc_cache.cached_power == 0) return_cmd_error(STR_ERROR_TRAIN_START_NO_CATENARY);
			break;

		case VEH_SHIP:
		case VEH_ROAD:
			break;

		case VEH_AIRCRAFT: {
			Aircraft *a = Aircraft::From(v);
			/* cannot stop airplane when in flight, or when taking off / landing */
			if (a->state >= STARTTAKEOFF && a->state < TERM7) return_cmd_error(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
		} break;

		default: return CMD_ERROR;
	}

	/* Check if this vehicle can be started/stopped. The callback will fail or
	 * return 0xFF if it can. */
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_START_STOP_CHECK, 0, 0, v->engine_type, v);
	if (callback != CALLBACK_FAILED && GB(callback, 0, 8) != 0xFF && HasBit(p2, 0)) {
		StringID error = GetGRFStringID(GetEngineGRFID(v->engine_type), 0xD000 + callback);
		return_cmd_error(error);
	}

	if (flags & DC_EXEC) {
		if (v->IsStoppedInDepot() && (flags & DC_AUTOREPLACE) == 0) DeleteVehicleNews(p1, STR_NEWS_TRAIN_IS_WAITING + v->type);

		v->vehstatus ^= VS_STOPPED;
		if (v->type != VEH_TRAIN) v->cur_speed = 0; // trains can stop 'slowly'
		v->MarkDirty();
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, VVW_WIDGET_START_STOP_VEH);
		SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
		SetWindowClassesDirty(GetWindowClassForVehicleType(v->type));
	}
	return CommandCost();
}

/** Starts or stops a lot of vehicles
 * @param tile Tile of the depot where the vehicles are started/stopped (only used for depots)
 * @param flags type of operation
 * @param p1 Station/Order/Depot ID (only used for vehicle list windows)
 * @param p2 bitmask
 *   - bit 0-4 Vehicle type
 *   - bit 5 false = start vehicles, true = stop vehicles
 *   - bit 6 if set, then it's a vehicle list window, not a depot and Tile is ignored in this case
 *   - bit 8-11 Vehicle List Window type (ignored unless bit 1 is set)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdMassStartStopVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleList list;
	VehicleType vehicle_type = (VehicleType)GB(p2, 0, 5);
	bool start_stop = HasBit(p2, 5);
	bool vehicle_list_window = HasBit(p2, 6);

	if (vehicle_list_window) {
		uint32 id = p1;
		uint16 window_type = p2 & VLW_MASK;

		GenerateVehicleSortList(&list, vehicle_type, _current_company, id, window_type);
	} else {
		/* Get the list of vehicles in the depot */
		BuildDepotVehicleList(vehicle_type, tile, &list, NULL);
	}

	for (uint i = 0; i < list.Length(); i++) {
		const Vehicle *v = list[i];

		if (!!(v->vehstatus & VS_STOPPED) != start_stop) continue;

		if (!vehicle_list_window) {
			if (vehicle_type == VEH_TRAIN) {
				if (!Train::From(v)->IsInDepot()) continue;
			} else {
				if (!(v->vehstatus & VS_HIDDEN)) continue;
			}
		}

		/* Just try and don't care if some vehicle's can't be stopped. */
		DoCommand(tile, v->index, 0, flags, CMD_START_STOP_VEHICLE);
	}

	return CommandCost();
}

/** Sells all vehicles in a depot
 * @param tile Tile of the depot where the depot is
 * @param flags type of operation
 * @param p1 Vehicle type
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDepotSellAllVehicles(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleList list;

	CommandCost cost(EXPENSES_NEW_VEHICLES);
	VehicleType vehicle_type = (VehicleType)GB(p1, 0, 8);
	uint sell_command = GetCmdSellVeh(vehicle_type);

	/* Get the list of vehicles in the depot */
	BuildDepotVehicleList(vehicle_type, tile, &list, &list);

	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	for (uint i = 0; i < list.Length(); i++) {
		CommandCost ret = DoCommand(tile, list[i]->index, 1, flags, sell_command);
		if (ret.Succeeded()) {
			cost.AddCost(ret);
			had_success = true;
		} else {
			last_error = ret;
		}
	}

	return had_success ? cost : last_error;
}

/**
 * Autoreplace all vehicles in the depot
 * @param tile Tile of the depot where the vehicles are
 * @param flags type of operation
 * @param p1 Type of vehicle
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDepotMassAutoReplace(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleList list;
	CommandCost cost = CommandCost(EXPENSES_NEW_VEHICLES);
	VehicleType vehicle_type = (VehicleType)GB(p1, 0, 8);

	if (!IsDepotTile(tile) || !IsTileOwner(tile, _current_company)) return CMD_ERROR;

	/* Get the list of vehicles in the depot */
	BuildDepotVehicleList(vehicle_type, tile, &list, &list, true);

	for (uint i = 0; i < list.Length(); i++) {
		const Vehicle *v = list[i];

		/* Ensure that the vehicle completely in the depot */
		if (!v->IsInDepot()) continue;

		CommandCost ret = DoCommand(0, v->index, 0, flags, CMD_AUTOREPLACE_VEHICLE);

		if (ret.Succeeded()) cost.AddCost(ret);
	}
	return cost;
}

/** Learn the price of refitting a certain engine
 * @param engine_type Which engine to refit
 * @return Price for refitting
 */
static CommandCost GetRefitCost(EngineID engine_type)
{
	ExpensesType expense_type;
	const Engine *e = Engine::Get(engine_type);
	Price base_price;
	uint cost_factor = e->info.refit_cost;
	switch (e->type) {
		case VEH_SHIP:
			base_price = PR_BUILD_VEHICLE_SHIP;
			expense_type = EXPENSES_SHIP_RUN;
			break;

		case VEH_ROAD:
			base_price = PR_BUILD_VEHICLE_ROAD;
			expense_type = EXPENSES_ROADVEH_RUN;
			break;

		case VEH_AIRCRAFT:
			base_price = PR_BUILD_VEHICLE_AIRCRAFT;
			expense_type = EXPENSES_AIRCRAFT_RUN;
			break;

		case VEH_TRAIN:
			base_price = (e->u.rail.railveh_type == RAILVEH_WAGON) ? PR_BUILD_VEHICLE_WAGON : PR_BUILD_VEHICLE_TRAIN;
			cost_factor <<= 1;
			expense_type = EXPENSES_TRAIN_RUN;
			break;

		default: NOT_REACHED();
	}
	return CommandCost(expense_type, GetPrice(base_price, cost_factor, e->grffile, -10));
}

/**
 * Refits a vehicle (chain).
 * This is the vehicle-type independent part of the CmdRefitXXX functions.
 * @param v            The vehicle to refit.
 * @param only_this    Whether to only refit this vehicle, or the whole chain.
 * @param new_cid      Cargotype to refit to
 * @param new_subtype  Cargo subtype to refit to
 * @param flags        Command flags
 * @return Refit cost.
 */
CommandCost RefitVehicle(Vehicle *v, bool only_this, CargoID new_cid, byte new_subtype, DoCommandFlag flags)
{
	CommandCost cost(v->GetExpenseType(false));
	uint total_capacity = 0;

	v->InvalidateNewGRFCacheOfChain();
	for (; v != NULL; v = (only_this ? NULL : v->Next())) {
		const Engine *e = Engine::Get(v->engine_type);
		if (!e->CanCarryCargo() || !HasBit(e->info.refit_mask, new_cid)) continue;

		/* Back up the vehicle's cargo type */
		CargoID temp_cid = v->cargo_type;
		byte temp_subtype = v->cargo_subtype;
		v->cargo_type = new_cid;
		v->cargo_subtype = new_subtype;

		uint16 mail_capacity;
		uint amount = GetVehicleCapacity(v, &mail_capacity);
		total_capacity += amount;

		/* Restore the original cargo type */
		v->cargo_type = temp_cid;
		v->cargo_subtype = temp_subtype;

		if (new_cid != v->cargo_type) {
			cost.AddCost(GetRefitCost(v->engine_type));
		}

		if (flags & DC_EXEC) {
			v->cargo.Truncate((v->cargo_type == new_cid) ? amount : 0);
			v->cargo_type = new_cid;
			v->cargo_cap = amount;
			v->cargo_subtype = new_subtype;
			if (v->type == VEH_AIRCRAFT) {
				Vehicle *u = v->Next();
				u->cargo_cap = mail_capacity;
				u->cargo.Truncate(mail_capacity);
			}
		}
	}

	_returned_refit_capacity = total_capacity;
	return cost;
}

/** Test if a name is unique among vehicle names.
 * @param name Name to test.
 * @return True ifffffff the name is unique.
 */
static bool IsUniqueVehicleName(const char *name)
{
	const Vehicle *v;

	FOR_ALL_VEHICLES(v) {
		if (v->name != NULL && strcmp(v->name, name) == 0) return false;
	}

	return true;
}

/** Clone the custom name of a vehicle, adding or incrementing a number.
 * @param src Source vehicle, with a custom name.
 * @param dst Destination vehicle.
 */
static void CloneVehicleName(const Vehicle *src, Vehicle *dst)
{
	char buf[256];

	/* Find the position of the first digit in the last group of digits. */
	size_t number_position;
	for (number_position = strlen(src->name); number_position > 0; number_position--) {
		/* The design of UTF-8 lets this work simply without having to check
		 * for UTF-8 sequences. */
		if (src->name[number_position - 1] < '0' || src->name[number_position - 1] > '9') break;
	}

	/* Format buffer and determine starting number. */
	int num;
	byte padding = 0;
	if (number_position == strlen(src->name)) {
		/* No digit at the end, so start at number 2. */
		strecpy(buf, src->name, lastof(buf));
		strecat(buf, " ", lastof(buf));
		number_position = strlen(buf);
		num = 2;
	} else {
		/* Found digits, parse them and start at the next number. */
		strecpy(buf, src->name, lastof(buf));
		buf[number_position] = '\0';
		char *endptr;
		num = strtol(&src->name[number_position], &endptr, 10) + 1;
		padding = endptr - &src->name[number_position];
	}

	/* Check if this name is already taken. */
	for (int max_iterations = 1000; max_iterations > 0; max_iterations--, num++) {
		/* Attach the number to the temporary name. */
		seprintf(&buf[number_position], lastof(buf), "%0*d", padding, num);

		/* Check the name is unique. */
		if (IsUniqueVehicleName(buf)) {
			dst->name = strdup(buf);
			break;
		}
	}

	/* All done. If we didn't find a name, it'll just use its default. */
}

/** Clone a vehicle. If it is a train, it will clone all the cars too
 * @param tile tile of the depot where the cloned vehicle is build
 * @param flags type of operation
 * @param p1 the original vehicle's index
 * @param p2 1 = shared orders, else copied orders
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost total_cost(EXPENSES_NEW_VEHICLES);
	uint32 build_argument = 2;

	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == NULL) return CMD_ERROR;
	Vehicle *v_front = v;
	Vehicle *w = NULL;
	Vehicle *w_front = NULL;
	Vehicle *w_rear = NULL;

	/*
	 * v_front is the front engine in the original vehicle
	 * v is the car/vehicle of the original vehicle, that is currently being copied
	 * w_front is the front engine of the cloned vehicle
	 * w is the car/vehicle currently being cloned
	 * w_rear is the rear end of the cloned train. It's used to add more cars and is only used by trains
	 */

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->type == VEH_TRAIN && (!Train::From(v)->IsFrontEngine() || Train::From(v)->crash_anim_pos >= 4400)) return CMD_ERROR;

	/* check that we can allocate enough vehicles */
	if (!(flags & DC_EXEC)) {
		int veh_counter = 0;
		do {
			veh_counter++;
		} while ((v = v->Next()) != NULL);

		if (!Vehicle::CanAllocateItem(veh_counter)) {
			return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
		}
	}

	v = v_front;

	do {
		if (v->type == VEH_TRAIN && Train::From(v)->IsRearDualheaded()) {
			/* we build the rear ends of multiheaded trains with the front ones */
			continue;
		}

		/* In case we're building a multi headed vehicle and the maximum number of
		 * vehicles is almost reached (e.g. max trains - 1) not all vehicles would
		 * be cloned. When the non-primary engines were build they were seen as
		 * 'new' vehicles whereas they would immediately be joined with a primary
		 * engine. This caused the vehicle to be not build as 'the limit' had been
		 * reached, resulting in partially build vehicles and such. */
		DoCommandFlag build_flags = flags;
		if ((flags & DC_EXEC) && !v->IsPrimaryVehicle()) build_flags |= DC_AUTOREPLACE;

		CommandCost cost = DoCommand(tile, v->engine_type, build_argument, build_flags, GetCmdBuildVeh(v));
		build_argument = 3; // ensure that we only assign a number to the first engine

		if (cost.Failed()) {
			/* Can't build a part, then sell the stuff we already made; clear up the mess */
			if (w_front != NULL) DoCommand(w_front->tile, w_front->index, 1, flags, GetCmdSellVeh(w_front));
			return cost;
		}

		total_cost.AddCost(cost);

		if (flags & DC_EXEC) {
			w = Vehicle::Get(_new_vehicle_id);

			if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) {
				SetBit(Train::From(w)->flags, VRF_REVERSE_DIRECTION);
			}

			if (v->type == VEH_TRAIN && !Train::From(v)->IsFrontEngine()) {
				/* this s a train car
				 * add this unit to the end of the train */
				CommandCost result = DoCommand(0, (w_rear->index << 16) | w->index, 1, flags, CMD_MOVE_RAIL_VEHICLE);
				if (result.Failed()) {
					/* The train can't be joined to make the same consist as the original.
					 * Sell what we already made (clean up) and return an error.           */
					DoCommand(w_front->tile, w_front->index, 1, flags, GetCmdSellVeh(w_front));
					DoCommand(w_front->tile, w->index,       1, flags, GetCmdSellVeh(w));
					return result; // return error and the message returned from CMD_MOVE_RAIL_VEHICLE
				}
			} else {
				/* this is a front engine or not a train. */
				w_front = w;
				w->service_interval = v->service_interval;
			}
			w_rear = w; // trains needs to know the last car in the train, so they can add more in next loop
		}
	} while (v->type == VEH_TRAIN && (v = Train::From(v)->GetNextVehicle()) != NULL);

	if ((flags & DC_EXEC) && v_front->type == VEH_TRAIN) {
		/* for trains this needs to be the front engine due to the callback function */
		_new_vehicle_id = w_front->index;
	}

	if (flags & DC_EXEC) {
		/* Cloned vehicles belong to the same group */
		DoCommand(0, v_front->group_id, w_front->index, flags, CMD_ADD_VEHICLE_GROUP);
	}


	/* Take care of refitting. */
	w = w_front;
	v = v_front;

	/* Both building and refitting are influenced by newgrf callbacks, which
	 * makes it impossible to accurately estimate the cloning costs. In
	 * particular, it is possible for engines of the same type to be built with
	 * different numbers of articulated parts, so when refitting we have to
	 * loop over real vehicles first, and then the articulated parts of those
	 * vehicles in a different loop. */
	do {
		do {
			if (flags & DC_EXEC) {
				assert(w != NULL);

				/* Find out what's the best sub type */
				byte subtype = GetBestFittingSubType(v, w);
				if (w->cargo_type != v->cargo_type || w->cargo_subtype != subtype) {
					CommandCost cost = DoCommand(0, w->index, v->cargo_type | (subtype << 8) | 1U << 16, flags, GetCmdRefitVeh(v));
					if (cost.Succeeded()) total_cost.AddCost(cost);
				}

				if (w->type == VEH_TRAIN && Train::From(w)->HasArticulatedPart()) {
					w = Train::From(w)->GetNextArticPart();
				} else if (w->type == VEH_ROAD && RoadVehicle::From(w)->HasArticulatedPart()) {
					w = w->Next();
				} else {
					break;
				}
			} else {
				const Engine *e = Engine::Get(v->engine_type);
				CargoID initial_cargo = (e->CanCarryCargo() ? e->GetDefaultCargoType() : (CargoID)CT_INVALID);

				if (v->cargo_type != initial_cargo && initial_cargo != CT_INVALID) {
					total_cost.AddCost(GetRefitCost(v->engine_type));
				}
			}

			if (v->type == VEH_TRAIN && Train::From(v)->HasArticulatedPart()) {
				v = Train::From(v)->GetNextArticPart();
			} else if (v->type == VEH_ROAD && RoadVehicle::From(v)->HasArticulatedPart()) {
				v = v->Next();
			} else {
				break;
			}
		} while (v != NULL);

		if ((flags & DC_EXEC) && v->type == VEH_TRAIN) w = Train::From(w)->GetNextVehicle();
	} while (v->type == VEH_TRAIN && (v = Train::From(v)->GetNextVehicle()) != NULL);

	if (flags & DC_EXEC) {
		/*
		 * Set the orders of the vehicle. Cannot do it earlier as we need
		 * the vehicle refitted before doing this, otherwise the moved
		 * cargo types might not match (passenger vs non-passenger)
		 */
		DoCommand(0, (v_front->index << 16) | w_front->index, p2 & 1 ? CO_SHARE : CO_COPY, flags, CMD_CLONE_ORDER);

		/* Now clone the vehicle's name, if it has one. */
		if (v_front->name != NULL) CloneVehicleName(v_front, w_front);
	}

	/* Since we can't estimate the cost of cloning a vehicle accurately we must
	 * check whether the company has enough money manually. */
	if (!CheckCompanyHasMoney(total_cost)) {
		if (flags & DC_EXEC) {
			/* The vehicle has already been bought, so now it must be sold again. */
			DoCommand(w_front->tile, w_front->index, 1, flags, GetCmdSellVeh(w_front));
		}
		return total_cost;
	}

	return total_cost;
}

/**
 * Send all vehicles of type to depots
 * @param type type of vehicle
 * @param flags the flags used for DoCommand()
 * @param service should the vehicles only get service in the depots
 * @param owner owner of the vehicles to send
 * @param vlw_flag tells what kind of list requested the goto depot
 * @param id general purpose id whoms meaning is given by @c vlw_flag; e.g. StationID for station lists
 * @return 0 for success and CMD_ERROR if no vehicle is able to go to depot
 */
CommandCost SendAllVehiclesToDepot(VehicleType type, DoCommandFlag flags, bool service, Owner owner, uint16 vlw_flag, uint32 id)
{
	VehicleList list;

	GenerateVehicleSortList(&list, type, owner, id, vlw_flag);

	/* Send all the vehicles to a depot */
	bool had_success = false;
	for (uint i = 0; i < list.Length(); i++) {
		const Vehicle *v = list[i];
		CommandCost ret = DoCommand(v->tile, v->index, (service ? 1 : 0) | DEPOT_DONT_CANCEL, flags, GetCmdSendToDepot(type));

		if (ret.Succeeded()) {
			had_success = true;

			/* Return 0 if DC_EXEC is not set this is a valid goto depot command)
			 * In this case we know that at least one vehicle can be sent to a depot
			 * and we will issue the command. We can now safely quit the loop, knowing
			 * it will succeed at least once. With DC_EXEC we really need to send them to the depot */
			if (!(flags & DC_EXEC)) break;
		}
	}

	return had_success ? CommandCost() : CMD_ERROR;
}

/** Give a custom name to your vehicle
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle ID to name
 * @param p2 unused
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameVehicle(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == NULL) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	bool reset = StrEmpty(text);

	if (!reset) {
		if (strlen(text) >= MAX_LENGTH_VEHICLE_NAME_BYTES) return CMD_ERROR;
		if (!(flags & DC_AUTOREPLACE) && !IsUniqueVehicleName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		free(v->name);
		v->name = reset ? NULL : strdup(text);
		InvalidateWindowClassesData(WC_TRAINS_LIST, 1);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}


/** Change the service interval of a vehicle
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle ID that is being service-interval-changed
 * @param p2 new service interval
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeServiceInt(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == NULL) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	uint16 serv_int = GetServiceIntervalClamped(p2, v->owner); // Double check the service interval from the user-input
	if (serv_int != p2) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->service_interval = serv_int;
		SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	}

	return CommandCost();
}
