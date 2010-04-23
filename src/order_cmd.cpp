/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_cmd.cpp Handling of orders. */

#include "stdafx.h"
#include "debug.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "company_func.h"
#include "news_func.h"
#include "vehicle_gui.h"
#include "strings_func.h"
#include "functions.h"
#include "window_func.h"
#include "timetable.h"
#include "vehicle_func.h"
#include "depot_base.h"
#include "core/pool_func.hpp"
#include "aircraft.h"
#include "roadveh.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "company_base.h"

#include "table/strings.h"

/* DestinationID must be at least as large as every these below, because it can
 * be any of them
 */
assert_compile(sizeof(DestinationID) >= sizeof(DepotID));
assert_compile(sizeof(DestinationID) >= sizeof(StationID));

TileIndex _backup_orders_tile;
BackuppedOrders _backup_orders_data;

OrderPool _order_pool("Order");
INSTANTIATE_POOL_METHODS(Order)
OrderListPool _orderlist_pool("OrderList");
INSTANTIATE_POOL_METHODS(OrderList)

void Order::Free()
{
	this->type  = OT_NOTHING;
	this->flags = 0;
	this->dest  = 0;
	this->next  = NULL;
}

void Order::MakeGoToStation(StationID destination)
{
	this->type = OT_GOTO_STATION;
	this->flags = 0;
	this->dest = destination;
}

void Order::MakeGoToDepot(DepotID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type, OrderDepotActionFlags action, CargoID cargo, byte subtype)
{
	this->type = OT_GOTO_DEPOT;
	this->SetDepotOrderType(order);
	this->SetDepotActionType(action);
	this->SetNonStopType(non_stop_type);
	this->dest = destination;
	this->SetRefit(cargo, subtype);
}

void Order::MakeGoToWaypoint(StationID destination)
{
	this->type = OT_GOTO_WAYPOINT;
	this->flags = 0;
	this->dest = destination;
}

void Order::MakeLoading(bool ordered)
{
	this->type = OT_LOADING;
	if (!ordered) this->flags = 0;
}

void Order::MakeLeaveStation()
{
	this->type = OT_LEAVESTATION;
	this->flags = 0;
}

void Order::MakeDummy()
{
	this->type = OT_DUMMY;
	this->flags = 0;
}

void Order::MakeConditional(VehicleOrderID order)
{
	this->type = OT_CONDITIONAL;
	this->flags = order;
	this->dest = 0;
}

void Order::SetRefit(CargoID cargo, byte subtype)
{
	this->refit_cargo = cargo;
	this->refit_subtype = subtype;
}

bool Order::Equals(const Order &other) const
{
	/* In case of go to nearest depot orders we need "only" compare the flags
	 * with the other and not the nearest depot order bit or the actual
	 * destination because those get clear/filled in during the order
	 * evaluation. If we do not do this the order will continuously be seen as
	 * a different order and it will try to find a "nearest depot" every tick. */
	if ((this->type == OT_GOTO_DEPOT && this->type == other.type) &&
			((this->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0 ||
			 (other.GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0)) {
		return
			this->GetDepotOrderType() == other.GetDepotOrderType() &&
			(this->GetDepotActionType() & ~ODATFB_NEAREST_DEPOT) == (other.GetDepotActionType() & ~ODATFB_NEAREST_DEPOT);
	}

	return
			this->type  == other.type &&
			this->flags == other.flags &&
			this->dest  == other.dest;
}

uint32 Order::Pack() const
{
	return this->dest << 16 | this->flags << 8 | this->type;
}

uint16 Order::MapOldOrder() const
{
	uint16 order = this->GetType();
	switch (this->type) {
		case OT_GOTO_STATION:
			if (this->GetUnloadType() & OUFB_UNLOAD) SetBit(order, 5);
			if (this->GetLoadType() & OLFB_FULL_LOAD) SetBit(order, 6);
			if (this->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) SetBit(order, 7);
			order |= GB(this->GetDestination(), 0, 8) << 8;
			break;
		case OT_GOTO_DEPOT:
			if (!(this->GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) SetBit(order, 6);
			SetBit(order, 7);
			order |= GB(this->GetDestination(), 0, 8) << 8;
			break;
		case OT_LOADING:
			if (this->GetLoadType() & OLFB_FULL_LOAD) SetBit(order, 6);
			break;
	}
	return order;
}

Order::Order(uint32 packed)
{
	this->type    = (OrderType)GB(packed,  0,  8);
	this->flags   = GB(packed,  8,  8);
	this->dest    = GB(packed, 16, 16);
	this->next    = NULL;
	this->refit_cargo   = CT_NO_REFIT;
	this->refit_subtype = 0;
	this->wait_time     = 0;
	this->travel_time   = 0;
}

/**
 *
 * Updates the widgets of a vehicle which contains the order-data
 *
 */
void InvalidateVehicleOrder(const Vehicle *v, int data)
{
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);

	if (data != 0) {
		/* Calls SetDirty() too */
		InvalidateWindowData(WC_VEHICLE_ORDERS,    v->index, data);
		InvalidateWindowData(WC_VEHICLE_TIMETABLE, v->index, data);
		return;
	}

	SetWindowDirty(WC_VEHICLE_ORDERS,    v->index);
	SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
}

/**
 *
 * Assign data to an order (from another order)
 *   This function makes sure that the index is maintained correctly
 *
 */
void Order::AssignOrder(const Order &other)
{
	this->type  = other.type;
	this->flags = other.flags;
	this->dest  = other.dest;

	this->refit_cargo   = other.refit_cargo;
	this->refit_subtype = other.refit_subtype;

	this->wait_time   = other.wait_time;
	this->travel_time = other.travel_time;
}

void OrderList::Initialize(Order *chain, Vehicle *v)
{
	this->first = chain;
	this->first_shared = v;

	this->num_orders = 0;
	this->num_vehicles = 1;
	this->timetable_duration = 0;

	for (Order *o = this->first; o != NULL; o = o->next) {
		++this->num_orders;
		this->timetable_duration += o->wait_time + o->travel_time;
	}

	for (Vehicle *u = this->first_shared->PreviousShared(); u != NULL; u = u->PreviousShared()) {
		++this->num_vehicles;
		this->first_shared = u;
	}

	for (const Vehicle *u = v->NextShared(); u != NULL; u = u->NextShared()) ++this->num_vehicles;
}

void OrderList::FreeChain(bool keep_orderlist)
{
	Order *next;
	for (Order *o = this->first; o != NULL; o = next) {
		next = o->next;
		delete o;
	}

	if (keep_orderlist) {
		this->first = NULL;
		this->num_orders = 0;
		this->timetable_duration = 0;
	} else {
		delete this;
	}
}

Order *OrderList::GetOrderAt(int index) const
{
	if (index < 0) return NULL;

	Order *order = this->first;

	while (order != NULL && index-- > 0)
		order = order->next;

	return order;
}

void OrderList::InsertOrderAt(Order *new_order, int index)
{
	if (this->first == NULL) {
		this->first = new_order;
	} else {
		if (index == 0) {
			/* Insert as first or only order */
			new_order->next = this->first;
			this->first = new_order;
		} else if (index >= this->num_orders) {
			/* index is after the last order, add it to the end */
			this->GetLastOrder()->next = new_order;
		} else {
			/* Put the new order in between */
			Order *order = this->GetOrderAt(index - 1);
			new_order->next = order->next;
			order->next = new_order;
		}
	}
	++this->num_orders;
	this->timetable_duration += new_order->wait_time + new_order->travel_time;
}


void OrderList::DeleteOrderAt(int index)
{
	if (index >= this->num_orders) return;

	Order *to_remove;

	if (index == 0) {
		to_remove = this->first;
		this->first = to_remove->next;
	} else {
		Order *prev = GetOrderAt(index - 1);
		to_remove = prev->next;
		prev->next = to_remove->next;
	}
	--this->num_orders;
	this->timetable_duration -= (to_remove->wait_time + to_remove->travel_time);
	delete to_remove;
}

void OrderList::MoveOrder(int from, int to)
{
	if (from >= this->num_orders || to >= this->num_orders || from == to) return;

	Order *moving_one;

	/* Take the moving order out of the pointer-chain */
	if (from == 0) {
		moving_one = this->first;
		this->first = moving_one->next;
	} else {
		Order *one_before = GetOrderAt(from - 1);
		moving_one = one_before->next;
		one_before->next = moving_one->next;
	}

	/* Insert the moving_order again in the pointer-chain */
	if (to == 0) {
		moving_one->next = this->first;
		this->first = moving_one;
	} else {
		Order *one_before = GetOrderAt(to - 1);
		moving_one->next = one_before->next;
		one_before->next = moving_one;
	}
}

void OrderList::RemoveVehicle(Vehicle *v)
{
	--this->num_vehicles;
	if (v == this->first_shared) this->first_shared = v->NextShared();
}

bool OrderList::IsVehicleInSharedOrdersList(const Vehicle *v) const
{
	for (const Vehicle *v_shared = this->first_shared; v_shared != NULL; v_shared = v_shared->NextShared()) {
		if (v_shared == v) return true;
	}

	return false;
}

int OrderList::GetPositionInSharedOrderList(const Vehicle *v) const
{
	int count = 0;
	for (const Vehicle *v_shared = v->PreviousShared(); v_shared != NULL; v_shared = v_shared->PreviousShared()) count++;
	return count;
}

bool OrderList::IsCompleteTimetable() const
{
	for (Order *o = this->first; o != NULL; o = o->next) {
		if (!o->IsCompletelyTimetabled()) return false;
	}
	return true;
}

void OrderList::DebugCheckSanity() const
{
	VehicleOrderID check_num_orders = 0;
	uint check_num_vehicles = 0;
	Ticks check_timetable_duration = 0;

	DEBUG(misc, 6, "Checking OrderList %hu for sanity...", this->index);

	for (const Order *o = this->first; o != NULL; o = o->next) {
		++check_num_orders;
		check_timetable_duration += o->wait_time + o->travel_time;
	}
	assert(this->num_orders == check_num_orders);
	assert(this->timetable_duration == check_timetable_duration);

	for (const Vehicle *v = this->first_shared; v != NULL; v = v->NextShared()) {
		++check_num_vehicles;
		assert(v->orders.list == this);
	}
	assert(this->num_vehicles == check_num_vehicles);
	DEBUG(misc, 6, "... detected %u orders, %u vehicles, %i ticks", (uint)this->num_orders,
	      this->num_vehicles, this->timetable_duration);
}

/**
 * Checks whether the order goes to a station or not, i.e. whether the
 * destination is a station
 * @param v the vehicle to check for
 * @param o the order to check
 * @return true if the destination is a station
 */
static inline bool OrderGoesToStation(const Vehicle *v, const Order *o)
{
	return o->IsType(OT_GOTO_STATION) ||
			(v->type == VEH_AIRCRAFT && o->IsType(OT_GOTO_DEPOT) && !(o->GetDepotActionType() & ODATFB_NEAREST_DEPOT));
}

/**
 * Delete all news items regarding defective orders about a vehicle
 * This could kill still valid warnings (for example about void order when just
 * another order gets added), but assume the company will notice the problems,
 * when (s)he's changing the orders.
 */
static void DeleteOrderWarnings(const Vehicle *v)
{
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_TOO_FEW_ORDERS);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_VOID_ORDER);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_DUPLICATE_ENTRY);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_INVALID_ENTRY);
}

/**
 * Returns a tile somewhat representing the order destination (not suitable for pathfinding).
 * @param v The vehicle to get the location for.
 * @return destination of order, or INVALID_TILE if none.
 */
TileIndex Order::GetLocation(const Vehicle *v) const
{
	switch (this->GetType()) {
		case OT_GOTO_WAYPOINT:
		case OT_GOTO_STATION:
			return BaseStation::Get(this->GetDestination())->xy;

		case OT_GOTO_DEPOT:
			if ((this->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0) return INVALID_TILE;
			return (v->type == VEH_AIRCRAFT) ? Station::Get(this->GetDestination())->xy : Depot::Get(this->GetDestination())->xy;

		default:
			return INVALID_TILE;
	}
}

static uint GetOrderDistance(const Order *prev, const Order *cur, const Vehicle *v, int conditional_depth = 0)
{
	assert(v->type == VEH_SHIP);

	if (cur->IsType(OT_CONDITIONAL)) {
		if (conditional_depth > v->GetNumOrders()) return 0;

		conditional_depth++;

		int dist1 = GetOrderDistance(prev, v->GetOrder(cur->GetConditionSkipToOrder()), v, conditional_depth);
		int dist2 = GetOrderDistance(prev, cur->next == NULL ? v->orders.list->GetFirstOrder() : cur->next, v, conditional_depth);
		return max(dist1, dist2);
	}

	TileIndex prev_tile = prev->GetLocation(v);
	TileIndex cur_tile = cur->GetLocation(v);
	if (prev_tile == INVALID_TILE || cur_tile == INVALID_TILE) return 0;
	return DistanceManhattan(prev_tile, cur_tile);
}

/** Add an order to the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - ID of the vehicle
 * - p1 = (bit 16 - 31) - the selected order (if any). If the last order is given,
 *                        the order will be inserted before that one
 *                        the maximum vehicle order id is 254.
 * @param p2 packed order to insert
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdInsertOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh   = GB(p1,  0, 16);
	VehicleOrderID sel_ord = GB(p1, 16, 16);
	Order new_order(p2);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Check if the inserted order is to the correct destination (owner, type),
	 * and has the correct flags if any */
	switch (new_order.GetType()) {
		case OT_GOTO_STATION: {
			const Station *st = Station::GetIfValid(new_order.GetDestination());
			if (st == NULL) return CMD_ERROR;

			if (st->owner != OWNER_NONE) {
				CommandCost ret = CheckOwnership(st->owner);
				if (ret.Failed()) return ret;
			}

			if (!CanVehicleUseStation(v, st)) return_cmd_error(STR_ERROR_CAN_T_ADD_ORDER);
			for (Vehicle *u = v->FirstShared(); u != NULL; u = u->NextShared()) {
				if (!CanVehicleUseStation(u, st)) return_cmd_error(STR_ERROR_CAN_T_ADD_ORDER_SHARED);
			}

			/* Non stop not allowed for non-trains. */
			if (new_order.GetNonStopType() != ONSF_STOP_EVERYWHERE && v->type != VEH_TRAIN && v->type != VEH_ROAD) return CMD_ERROR;

			/* No load and no unload are mutual exclusive. */
			if ((new_order.GetLoadType() & OLFB_NO_LOAD) && (new_order.GetUnloadType() & OUFB_NO_UNLOAD)) return CMD_ERROR;

			/* Filter invalid load/unload types. */
			switch (new_order.GetLoadType()) {
				case OLF_LOAD_IF_POSSIBLE: case OLFB_FULL_LOAD: case OLF_FULL_LOAD_ANY: case OLFB_NO_LOAD: break;
				default: return CMD_ERROR;
			}
			switch (new_order.GetUnloadType()) {
				case OUF_UNLOAD_IF_POSSIBLE: case OUFB_UNLOAD: case OUFB_TRANSFER: case OUFB_NO_UNLOAD: break;
				default: return CMD_ERROR;
			}

			/* Filter invalid stop locations */
			switch (new_order.GetStopLocation()) {
				case OSL_PLATFORM_NEAR_END:
				case OSL_PLATFORM_MIDDLE:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					/* FALL THROUGH */
				case OSL_PLATFORM_FAR_END:
					break;

				default:
					return CMD_ERROR;
			}

			break;
		}

		case OT_GOTO_DEPOT: {
			if (new_order.GetDepotActionType() != ODATFB_NEAREST_DEPOT) {
				if (v->type == VEH_AIRCRAFT) {
					const Station *st = Station::GetIfValid(new_order.GetDestination());

					if (st == NULL) return CMD_ERROR;

					CommandCost ret = CheckOwnership(st->owner);
					if (ret.Failed()) return ret;

					if (!CanVehicleUseStation(v, st) || !st->airport.HasHangar()) {
						return CMD_ERROR;
					}
				} else {
					const Depot *dp = Depot::GetIfValid(new_order.GetDestination());

					if (dp == NULL) return CMD_ERROR;

					CommandCost ret = CheckOwnership(GetTileOwner(dp->xy));
					if (ret.Failed()) return ret;

					switch (v->type) {
						case VEH_TRAIN:
							if (!IsRailDepotTile(dp->xy)) return CMD_ERROR;
							break;

						case VEH_ROAD:
							if (!IsRoadDepotTile(dp->xy)) return CMD_ERROR;
							break;

						case VEH_SHIP:
							if (!IsShipDepotTile(dp->xy)) return CMD_ERROR;
							break;

						default: return CMD_ERROR;
					}
				}
			}

			if (new_order.GetNonStopType() != ONSF_STOP_EVERYWHERE && v->type != VEH_TRAIN && v->type != VEH_ROAD) return CMD_ERROR;
			if (new_order.GetDepotOrderType() & ~(ODTFB_PART_OF_ORDERS | ((new_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) != 0 ? ODTFB_SERVICE : 0))) return CMD_ERROR;
			if (new_order.GetDepotActionType() & ~(ODATFB_HALT | ODATFB_NEAREST_DEPOT)) return CMD_ERROR;
			if ((new_order.GetDepotOrderType() & ODTFB_SERVICE) && (new_order.GetDepotActionType() & ODATFB_HALT)) return CMD_ERROR;
			break;
		}

		case OT_GOTO_WAYPOINT: {
			const Waypoint *wp = Waypoint::GetIfValid(new_order.GetDestination());
			if (wp == NULL) return CMD_ERROR;

			switch (v->type) {
				default: return CMD_ERROR;

				case VEH_TRAIN: {
					if (!(wp->facilities & FACIL_TRAIN)) return_cmd_error(STR_ERROR_CAN_T_ADD_ORDER);

					CommandCost ret = CheckOwnership(wp->owner);
					if (ret.Failed()) return ret;
					break;
				}

				case VEH_SHIP:
					if (!(wp->facilities & FACIL_DOCK)) return_cmd_error(STR_ERROR_CAN_T_ADD_ORDER);
					if (wp->owner != OWNER_NONE) {
						CommandCost ret = CheckOwnership(wp->owner);
						if (ret.Failed()) return ret;
					}
					break;
			}

			/* Order flags can be any of the following for waypoints:
			 * [non-stop]
			 * non-stop orders (if any) are only valid for trains */
			if (new_order.GetNonStopType() != ONSF_STOP_EVERYWHERE && v->type != VEH_TRAIN) return CMD_ERROR;
			break;
		}

		case OT_CONDITIONAL: {
			VehicleOrderID skip_to = new_order.GetConditionSkipToOrder();
			if (skip_to != 0 && skip_to >= v->GetNumOrders()) return CMD_ERROR; // Always allow jumping to the first (even when there is no order).
			if (new_order.GetConditionVariable() > OCV_END) return CMD_ERROR;

			OrderConditionComparator occ = new_order.GetConditionComparator();
			if (occ > OCC_END) return CMD_ERROR;
			switch (new_order.GetConditionVariable()) {
				case OCV_REQUIRES_SERVICE:
					if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_UNCONDITIONALLY:
					if (occ != OCC_EQUALS) return CMD_ERROR;
					if (new_order.GetConditionValue() != 0) return CMD_ERROR;
					break;

				case OCV_LOAD_PERCENTAGE:
				case OCV_RELIABILITY:
					if (new_order.GetConditionValue() > 100) return CMD_ERROR;
					/* FALL THROUGH */
				default:
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;
			}
		} break;

		default: return CMD_ERROR;
	}

	if (sel_ord > v->GetNumOrders()) return CMD_ERROR;

	if (v->GetNumOrders() >= MAX_VEH_ORDER_ID) return_cmd_error(STR_ERROR_TOO_MANY_ORDERS);
	if (!Order::CanAllocateItem()) return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
	if (v->orders.list == NULL && !OrderList::CanAllocateItem()) return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);

	if (v->type == VEH_SHIP && _settings_game.pf.pathfinder_for_ships != VPF_NPF) {
		/* Make sure the new destination is not too far away from the previous */
		const Order *prev = NULL;
		uint n = 0;

		/* Find the last goto station or depot order before the insert location.
		 * If the order is to be inserted at the beginning of the order list this
		 * finds the last order in the list. */
		const Order *o;
		FOR_VEHICLE_ORDERS(v, o) {
			switch (o->GetType()) {
				case OT_GOTO_STATION:
				case OT_GOTO_DEPOT:
				case OT_GOTO_WAYPOINT:
					prev = o;
					break;

				default: break;
			}
			if (++n == sel_ord && prev != NULL) break;
		}
		if (prev != NULL) {
			uint dist = GetOrderDistance(prev, &new_order, v);
			if (dist >= 130) {
				return_cmd_error(STR_ERROR_TOO_FAR_FROM_PREVIOUS_DESTINATION);
			}
		}
	}

	if (flags & DC_EXEC) {
		Order *new_o = new Order();
		new_o->AssignOrder(new_order);

		/* Create new order and link in list */
		if (v->orders.list == NULL) {
			v->orders.list = new OrderList(new_o, v);
		} else {
			v->orders.list->InsertOrderAt(new_o, sel_ord);
		}

		Vehicle *u = v->FirstShared();
		DeleteOrderWarnings(u);
		for (; u != NULL; u = u->NextShared()) {
			assert(v->orders.list == u->orders.list);

			/* If there is added an order before the current one, we need
			to update the selected order */
			if (sel_ord <= u->cur_order_index) {
				uint cur = u->cur_order_index + 1;
				/* Check if we don't go out of bound */
				if (cur < u->GetNumOrders())
					u->cur_order_index = cur;
			}
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u, INVALID_VEH_ORDER_ID | (sel_ord << 8));
		}

		/* As we insert an order, the order to skip to will be 'wrong'. */
		VehicleOrderID cur_order_id = 0;
		Order *order;
		FOR_VEHICLE_ORDERS(v, order) {
			if (order->IsType(OT_CONDITIONAL)) {
				VehicleOrderID order_id = order->GetConditionSkipToOrder();
				if (order_id >= sel_ord) {
					order->SetConditionSkipToOrder(order_id + 1);
				}
				if (order_id == cur_order_id) {
					order->SetConditionSkipToOrder((order_id + 1) % v->GetNumOrders());
				}
			}
			cur_order_id++;
		}

		/* Make sure to rebuild the whole list */
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
	}

	return CommandCost();
}

/** Declone an order-list
 * @param *dst delete the orders of this vehicle
 * @param flags execution flags
 */
static CommandCost DecloneOrder(Vehicle *dst, DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		DeleteVehicleOrders(dst);
		InvalidateVehicleOrder(dst, -1);
		InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type), 0);
	}
	return CommandCost();
}

/** Delete an order from the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 the order to delete (max 255)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_id = p1;
	VehicleOrderID sel_ord = p2;
	Order *order;

	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* If we did not select an order, we maybe want to de-clone the orders */
	if (sel_ord >= v->GetNumOrders())
		return DecloneOrder(v, flags);

	order = v->GetOrder(sel_ord);
	if (order == NULL) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->DeleteOrderAt(sel_ord);

		Vehicle *u = v->FirstShared();
		DeleteOrderWarnings(u);
		for (; u != NULL; u = u->NextShared()) {
			if (sel_ord < u->cur_order_index)
				u->cur_order_index--;

			assert(v->orders.list == u->orders.list);

			/* NON-stop flag is misused to see if a train is in a station that is
			 * on his order list or not */
			if (sel_ord == u->cur_order_index && u->current_order.IsType(OT_LOADING)) {
				u->current_order.SetNonStopType(ONSF_STOP_EVERYWHERE);
			}

			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u, sel_ord | (INVALID_VEH_ORDER_ID << 8));
		}

		/* As we delete an order, the order to skip to will be 'wrong'. */
		VehicleOrderID cur_order_id = 0;
		FOR_VEHICLE_ORDERS(v, order) {
			if (order->IsType(OT_CONDITIONAL)) {
				VehicleOrderID order_id = order->GetConditionSkipToOrder();
				if (order_id >= sel_ord) {
					order->SetConditionSkipToOrder(max(order_id - 1, 0));
				}
				if (order_id == cur_order_id) {
					order->SetConditionSkipToOrder((order_id + 1) % v->GetNumOrders());
				}
			}
			cur_order_id++;
		}

		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
	}

	return CommandCost();
}

/** Goto order of order-list.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 The ID of the vehicle which order is skipped
 * @param p2 the selected order to which we want to skip
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSkipToOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_id = p1;
	VehicleOrderID sel_ord = p2;

	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == NULL || !v->IsPrimaryVehicle() || sel_ord == v->cur_order_index || sel_ord >= v->GetNumOrders() || v->GetNumOrders() < 2) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		v->cur_order_index = sel_ord;

		if (v->current_order.IsType(OT_LOADING)) v->LeaveStation();

		InvalidateVehicleOrder(v, -2);
	}

	/* We have an aircraft/ship, they have a mini-schedule, so update them all */
	if (v->type == VEH_AIRCRAFT) SetWindowClassesDirty(WC_AIRCRAFT_LIST);
	if (v->type == VEH_SHIP) SetWindowClassesDirty(WC_SHIPS_LIST);

	return CommandCost();
}

/**
 * Move an order inside the orderlist
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 order to move and target
 *           bit 0-15  : the order to move
 *           bit 16-31 : the target order
 * @param text unused
 * @return the cost of this operation or an error
 * @note The target order will move one place down in the orderlist
 *  if you move the order upwards else it'll move it one place down
 */
CommandCost CmdMoveOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = p1;
	VehicleOrderID moving_order = GB(p2,  0, 16);
	VehicleOrderID target_order = GB(p2, 16, 16);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Don't make senseless movements */
	if (moving_order >= v->GetNumOrders() || target_order >= v->GetNumOrders() ||
			moving_order == target_order || v->GetNumOrders() <= 1) return CMD_ERROR;

	Order *moving_one = v->GetOrder(moving_order);
	/* Don't move an empty order */
	if (moving_one == NULL) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->MoveOrder(moving_order, target_order);

		/* Update shared list */
		Vehicle *u = v->FirstShared();

		DeleteOrderWarnings(u);

		for (; u != NULL; u = u->NextShared()) {
			/* Update the current order */
			if (u->cur_order_index == moving_order) {
				u->cur_order_index = target_order;
			} else if (u->cur_order_index > moving_order && u->cur_order_index <= target_order) {
				u->cur_order_index--;
			} else if (u->cur_order_index < moving_order && u->cur_order_index >= target_order) {
				u->cur_order_index++;
			}

			assert(v->orders.list == u->orders.list);
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u, moving_order | (target_order << 8));
		}

		/* As we move an order, the order to skip to will be 'wrong'. */
		Order *order;
		FOR_VEHICLE_ORDERS(v, order) {
			if (order->IsType(OT_CONDITIONAL)) {
				VehicleOrderID order_id = order->GetConditionSkipToOrder();
				if (order_id == moving_order) {
					order_id = target_order;
				} else if (order_id > moving_order && order_id <= target_order) {
					order_id--;
				} else if (order_id < moving_order && order_id >= target_order) {
					order_id++;
				}
				order->SetConditionSkipToOrder(order_id);
			}
		}

		/* Make sure to rebuild the whole list */
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
	}

	return CommandCost();
}

/** Modify an order in the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - ID of the vehicle
 * - p1 = (bit 16 - 31) - the selected order (if any). If the last order is given,
 *                        the order will be inserted before that one
 *                        only the first 8 bits used currently (bit 16 - 23) (max 255)
 * @param p2 various bitstuffed elements
 *  - p2 = (bit 0 -  3) - what data to modify (@see ModifyOrderFlags)
 *  - p2 = (bit 4 - 15) - the data to modify
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdModifyOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleOrderID sel_ord = GB(p1, 16, 16); // XXX - automatically truncated to 8 bits.
	VehicleID veh          = GB(p1,  0, 16);
	ModifyOrderFlags mof   = Extract<ModifyOrderFlags, 0, 4>(p2);
	uint16 data            = GB(p2, 4, 11);

	if (mof >= MOF_END) return CMD_ERROR;

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Is it a valid order? */
	if (sel_ord >= v->GetNumOrders()) return CMD_ERROR;

	Order *order = v->GetOrder(sel_ord);
	switch (order->GetType()) {
		case OT_GOTO_STATION:
			if (mof == MOF_COND_VARIABLE || mof == MOF_COND_COMPARATOR || mof == MOF_DEPOT_ACTION || mof == MOF_COND_VALUE) return CMD_ERROR;
			break;

		case OT_GOTO_DEPOT:
			if (mof != MOF_NON_STOP && mof != MOF_DEPOT_ACTION) return CMD_ERROR;
			break;

		case OT_GOTO_WAYPOINT:
			if (mof != MOF_NON_STOP) return CMD_ERROR;
			break;

		case OT_CONDITIONAL:
			if (mof != MOF_COND_VARIABLE && mof != MOF_COND_COMPARATOR && mof != MOF_COND_VALUE && mof != MOF_COND_DESTINATION) return CMD_ERROR;
			break;

		default:
			return CMD_ERROR;
	}

	switch (mof) {
		default: NOT_REACHED();

		case MOF_NON_STOP:
			if (v->type != VEH_TRAIN && v->type != VEH_ROAD) return CMD_ERROR;
			if (data >= ONSF_END) return CMD_ERROR;
			if (data == order->GetNonStopType()) return CMD_ERROR;
			break;

		case MOF_STOP_LOCATION:
			if (v->type != VEH_TRAIN) return CMD_ERROR;
			if (data >= OSL_END) return CMD_ERROR;
			break;

		case MOF_UNLOAD:
			if ((data & ~(OUFB_UNLOAD | OUFB_TRANSFER | OUFB_NO_UNLOAD)) != 0) return CMD_ERROR;
			/* Unload and no-unload are mutual exclusive and so are transfer and no unload. */
			if (data != 0 && ((data & (OUFB_UNLOAD | OUFB_TRANSFER)) != 0) == ((data & OUFB_NO_UNLOAD) != 0)) return CMD_ERROR;
			if (data == order->GetUnloadType()) return CMD_ERROR;
			break;

		case MOF_LOAD:
			if (data > OLFB_NO_LOAD || data == 1) return CMD_ERROR;
			if (data == order->GetLoadType()) return CMD_ERROR;
			break;

		case MOF_DEPOT_ACTION:
			if (data >= DA_END) return CMD_ERROR;
			break;

		case MOF_COND_VARIABLE:
			if (data >= OCV_END) return CMD_ERROR;
			break;

		case MOF_COND_COMPARATOR:
			if (data >= OCC_END) return CMD_ERROR;
			switch (order->GetConditionVariable()) {
				case OCV_UNCONDITIONALLY: return CMD_ERROR;

				case OCV_REQUIRES_SERVICE:
					if (data != OCC_IS_TRUE && data != OCC_IS_FALSE) return CMD_ERROR;
					break;

				default:
					if (data == OCC_IS_TRUE || data == OCC_IS_FALSE) return CMD_ERROR;
					break;
			}
			break;

		case MOF_COND_VALUE:
			switch (order->GetConditionVariable()) {
				case OCV_UNCONDITIONALLY: return CMD_ERROR;

				case OCV_LOAD_PERCENTAGE:
				case OCV_RELIABILITY:
					if (data > 100) return CMD_ERROR;
					break;

				default:
					if (data > 2047) return CMD_ERROR;
					break;
			}
			break;

		case MOF_COND_DESTINATION:
			if (data >= v->GetNumOrders()) return CMD_ERROR;
			break;
	}

	if (flags & DC_EXEC) {
		switch (mof) {
			case MOF_NON_STOP:
				order->SetNonStopType((OrderNonStopFlags)data);
				break;

			case MOF_STOP_LOCATION:
				order->SetStopLocation((OrderStopLocation)data);
				break;

			case MOF_UNLOAD:
				order->SetUnloadType((OrderUnloadFlags)data);
				if ((data & OUFB_NO_UNLOAD) != 0 && (order->GetLoadType() & OLFB_NO_LOAD) != 0) {
					order->SetLoadType((OrderLoadFlags)(order->GetLoadType() & ~OLFB_NO_LOAD));
				}
				break;

			case MOF_LOAD:
				order->SetLoadType((OrderLoadFlags)data);
				if ((data & OLFB_NO_LOAD) != 0 && (order->GetUnloadType() & OUFB_NO_UNLOAD) != 0) {
					/* No load + no unload isn't compatible */
					order->SetUnloadType((OrderUnloadFlags)(order->GetUnloadType() & ~OUFB_NO_UNLOAD));
				}
				break;

			case MOF_DEPOT_ACTION: {
				switch (data) {
					case DA_ALWAYS_GO:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(order->GetDepotActionType() & ~ODATFB_HALT));
						break;

					case DA_SERVICE:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() | ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(order->GetDepotActionType() & ~ODATFB_HALT));
						break;

					case DA_STOP:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(order->GetDepotActionType() | ODATFB_HALT));
						break;

					default:
						NOT_REACHED();
				}
			} break;

			case MOF_COND_VARIABLE: {
				order->SetConditionVariable((OrderConditionVariable)data);

				OrderConditionComparator occ = order->GetConditionComparator();
				switch (order->GetConditionVariable()) {
					case OCV_UNCONDITIONALLY:
						order->SetConditionComparator(OCC_EQUALS);
						order->SetConditionValue(0);
						break;

					case OCV_REQUIRES_SERVICE:
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						break;

					case OCV_LOAD_PERCENTAGE:
					case OCV_RELIABILITY:
						if (order->GetConditionValue() > 100) order->SetConditionValue(100);
						/* FALL THROUGH */
					default:
						if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) order->SetConditionComparator(OCC_EQUALS);
						break;
				}
			} break;

			case MOF_COND_COMPARATOR:
				order->SetConditionComparator((OrderConditionComparator)data);
				break;

			case MOF_COND_VALUE:
				order->SetConditionValue(data);
				break;

			case MOF_COND_DESTINATION:
				order->SetConditionSkipToOrder(data);
				break;

			default: NOT_REACHED();
		}

		/* Update the windows and full load flags, also for vehicles that share the same order list */
		Vehicle *u = v->FirstShared();
		DeleteOrderWarnings(u);
		for (; u != NULL; u = u->NextShared()) {
			/* Toggle u->current_order "Full load" flag if it changed.
			 * However, as the same flag is used for depot orders, check
			 * whether we are not going to a depot as there are three
			 * cases where the full load flag can be active and only
			 * one case where the flag is used for depot orders. In the
			 * other cases for the OrderTypeByte the flags are not used,
			 * so do not care and those orders should not be active
			 * when this function is called.
			 */
			if (sel_ord == u->cur_order_index &&
					(u->current_order.IsType(OT_GOTO_STATION) || u->current_order.IsType(OT_LOADING)) &&
					u->current_order.GetLoadType() != order->GetLoadType()) {
				u->current_order.SetLoadType(order->GetLoadType());
			}
			InvalidateVehicleOrder(u, -2);
		}
	}

	return CommandCost();
}

/** Clone/share/copy an order-list of another vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0-15) - destination vehicle to clone orders to (p1 & 0xFFFF)
 * - p1 = (bit 16-31) - source vehicle to clone orders from, if any (none for CO_UNSHARE)
 * @param p2 mode of cloning: CO_SHARE, CO_COPY, or CO_UNSHARE
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_src = GB(p1, 16, 16);
	VehicleID veh_dst = GB(p1,  0, 16);

	Vehicle *dst = Vehicle::GetIfValid(veh_dst);
	if (dst == NULL || !dst->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(dst->owner);
	if (ret.Failed()) return ret;

	switch (p2) {
		case CO_SHARE: {
			Vehicle *src = Vehicle::GetIfValid(veh_src);

			/* Sanity checks */
			if (src == NULL || !src->IsPrimaryVehicle() || dst->type != src->type || dst == src) return CMD_ERROR;

			CommandCost ret = CheckOwnership(src->owner);
			if (ret.Failed()) return ret;

			/* Trucks can't share orders with busses (and visa versa) */
			if (src->type == VEH_ROAD && RoadVehicle::From(src)->IsBus() != RoadVehicle::From(dst)->IsBus()) {
				return CMD_ERROR;
			}

			/* Is the vehicle already in the shared list? */
			if (src->FirstShared() == dst->FirstShared()) return CMD_ERROR;

			const Order *order;

			FOR_VEHICLE_ORDERS(src, order) {
				if (OrderGoesToStation(dst, order) &&
						!CanVehicleUseStation(dst, Station::Get(order->GetDestination()))) {
					return_cmd_error(STR_ERROR_CAN_T_COPY_SHARE_ORDER);
				}
			}

			if (flags & DC_EXEC) {
				/* If the destination vehicle had a OrderList, destroy it */
				DeleteVehicleOrders(dst);

				dst->orders.list = src->orders.list;

				/* Link this vehicle in the shared-list */
				dst->AddToShared(src);

				InvalidateVehicleOrder(dst, -1);
				InvalidateVehicleOrder(src, -2);

				InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type), 0);
			}
		} break;

		case CO_COPY: {
			Vehicle *src = Vehicle::GetIfValid(veh_src);

			/* Sanity checks */
			if (src == NULL || !src->IsPrimaryVehicle() || dst->type != src->type || dst == src) return CMD_ERROR;

			CommandCost ret = CheckOwnership(src->owner);
			if (ret.Failed()) return ret;

			/* Trucks can't copy all the orders from busses (and visa versa),
			 * and neither can helicopters and aircarft. */
			const Order *order;
			FOR_VEHICLE_ORDERS(src, order) {
				if (OrderGoesToStation(dst, order) &&
						!CanVehicleUseStation(dst, Station::Get(order->GetDestination()))) {
					return_cmd_error(STR_ERROR_CAN_T_COPY_SHARE_ORDER);
				}
			}

			/* make sure there are orders available */
			int delta = dst->IsOrderListShared() ? src->GetNumOrders() + 1 : src->GetNumOrders() - dst->GetNumOrders();
			if (!Order::CanAllocateItem(delta) ||
					((dst->orders.list == NULL || dst->IsOrderListShared()) && !OrderList::CanAllocateItem())) {
				return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			}

			if (flags & DC_EXEC) {
				const Order *order;
				Order *first = NULL;
				Order **order_dst;

				/* If the destination vehicle had an order list, destroy the chain but keep the OrderList */
				DeleteVehicleOrders(dst, true);

				order_dst = &first;
				FOR_VEHICLE_ORDERS(src, order) {
					*order_dst = new Order();
					(*order_dst)->AssignOrder(*order);
					order_dst = &(*order_dst)->next;
				}
				if (dst->orders.list == NULL) {
					dst->orders.list = new OrderList(first, dst);
				} else {
					assert(dst->orders.list->GetFirstOrder() == NULL);
					assert(!dst->orders.list->IsShared());
					delete dst->orders.list;
					dst->orders.list = new OrderList(first, dst);
				}

				InvalidateVehicleOrder(dst, -1);

				InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type), 0);
			}
		} break;

		case CO_UNSHARE: return DecloneOrder(dst, flags);
		default: return CMD_ERROR;
	}

	return CommandCost();
}

/** Add/remove refit orders from an order
 * @param tile Not used
 * @param flags operation to perform
 * @param p1 VehicleIndex of the vehicle having the order
 * @param p2 bitmask
 *   - bit 0-7 CargoID
 *   - bit 8-15 Cargo subtype
 *   - bit 16-23 number of order to modify
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdOrderRefit(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 16);
	VehicleOrderID order_number  = GB(p2, 16, 8);
	CargoID cargo = GB(p2, 0, 8);
	byte subtype  = GB(p2, 8, 8);

	if (cargo >= NUM_CARGO) return CMD_ERROR;

	const Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	Order *order = v->GetOrder(order_number);
	if (order == NULL) return CMD_ERROR;

	if (flags & DC_EXEC) {
		order->SetRefit(cargo, subtype);

		for (Vehicle *u = v->FirstShared(); u != NULL; u = u->NextShared()) {
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u, -2);

			/* If the vehicle already got the current depot set as current order, then update current order as well */
			if (u->cur_order_index == order_number && (u->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) {
				u->current_order.SetRefit(cargo, subtype);
			}
		}
	}

	return CommandCost();
}

/**
 *
 * Backup a vehicle order-list, so you can replace a vehicle
 *  without losing the order-list
 *
 */
void BackupVehicleOrders(const Vehicle *v, BackuppedOrders *bak)
{
	/* Make sure we always have freed the stuff */
	free(bak->order);
	bak->order = NULL;
	free(bak->name);
	bak->name = NULL;

	/* Save general info */
	bak->orderindex       = v->cur_order_index;
	bak->group            = v->group_id;
	bak->service_interval = v->service_interval;
	if (v->name != NULL) bak->name = strdup(v->name);

	/* If we have shared orders, store it on a special way */
	if (v->IsOrderListShared()) {
		const Vehicle *u = (v->FirstShared() == v) ? v->NextShared() : v->FirstShared();

		bak->clone = u->index;
	} else {
		/* Else copy the orders */

		/* We do not have shared orders */
		bak->clone = INVALID_VEHICLE;


		/* Count the number of orders */
		uint cnt = 0;
		const Order *order;
		FOR_VEHICLE_ORDERS(v, order) cnt++;

		/* Allocate memory for the orders plus an end-of-orders marker */
		bak->order = MallocT<Order>(cnt + 1);

		Order *dest = bak->order;

		/* Copy the orders */
		FOR_VEHICLE_ORDERS(v, order) {
			memcpy(dest, order, sizeof(Order));
			dest++;
		}
		/* End the list with an empty order */
		dest->Free();
	}
}

/**
 *
 * Restore vehicle orders that are backupped via BackupVehicleOrders
 *
 */
void RestoreVehicleOrders(const Vehicle *v, const BackuppedOrders *bak)
{
	/* If we have a custom name, process that */
	if (bak->name != NULL) DoCommandP(0, v->index, 0, CMD_RENAME_VEHICLE, NULL, bak->name);

	/* If we had shared orders, recover that */
	if (bak->clone != INVALID_VEHICLE) {
		DoCommandP(0, v->index | (bak->clone << 16), CO_SHARE, CMD_CLONE_ORDER);
	} else {

		/* CMD_NO_TEST_IF_IN_NETWORK is used here, because CMD_INSERT_ORDER checks if the
		 *  order number is one more than the current amount of orders, and because
		 *  in network the commands are queued before send, the second insert always
		 *  fails in test mode. By bypassing the test-mode, that no longer is a problem. */
		for (uint i = 0; !bak->order[i].IsType(OT_NOTHING); i++) {
			Order o = bak->order[i];
			/* Conditional orders need to have their destination to be valid on insertion. */
			if (o.IsType(OT_CONDITIONAL)) o.SetConditionSkipToOrder(0);

			if (!DoCommandP(0, v->index + (i << 16), o.Pack(),
					CMD_INSERT_ORDER | CMD_NO_TEST_IF_IN_NETWORK)) {
				break;
			}

			/* Copy timetable if enabled */
			if (_settings_game.order.timetabling && !DoCommandP(0, v->index | (i << 16) | (1 << 25),
					o.wait_time << 16 | o.travel_time,
					CMD_CHANGE_TIMETABLE | CMD_NO_TEST_IF_IN_NETWORK)) {
				break;
			}
		}

			/* Fix the conditional orders' destination. */
		for (uint i = 0; !bak->order[i].IsType(OT_NOTHING); i++) {
			if (!bak->order[i].IsType(OT_CONDITIONAL)) continue;

			if (!DoCommandP(0, v->index + (i << 16), MOF_LOAD | (bak->order[i].GetConditionSkipToOrder() << 4),
					CMD_MODIFY_ORDER | CMD_NO_TEST_IF_IN_NETWORK)) {
				break;
			}
		}
	}

	/* Restore vehicle order-index and service interval */
	DoCommandP(0, v->index, bak->orderindex | (bak->service_interval << 16), CMD_RESTORE_ORDER_INDEX);

	/* Restore vehicle group */
	DoCommandP(0, bak->group, v->index, CMD_ADD_VEHICLE_GROUP);
}

/** Restore the current order-index of a vehicle and sets service-interval.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 various bistuffed elements
 * - p2 = (bit  0-15) - current order-index (p2 & 0xFFFF)
 * - p2 = (bit 16-31) - service interval (p2 >> 16)
 * @param text unused
 * @return the cost of this operation or an error
 * @todo Unfortunately you cannot safely restore the unitnumber or the old vehicle
 * as far as I can see. We can store it in BackuppedOrders, and restore it, but
 * but we have no way of seeing it has been tampered with or not, as we have no
 * legit way of knowing what that ID was.@n
 * If we do want to backup/restore it, just add UnitID uid to BackuppedOrders, and
 * restore it as parameter 'y' (ugly hack I know) for example. "v->unitnumber = y;"
 */
CommandCost CmdRestoreOrderIndex(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleOrderID cur_ord = GB(p2,  0, 16);
	uint16 serv_int = GB(p2, 16, 16);

	Vehicle *v = Vehicle::GetIfValid(p1);
	/* Check the vehicle type and ownership, and if the service interval and order are in range */
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (serv_int != GetServiceIntervalClamped(serv_int, v->owner) || cur_ord >= v->GetNumOrders()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->cur_order_index = cur_ord;
		v->service_interval = serv_int;
	}

	return CommandCost();
}


/**
 *
 * Check the orders of a vehicle, to see if there are invalid orders and stuff
 *
 */
void CheckOrders(const Vehicle *v)
{
	/* Does the user wants us to check things? */
	if (_settings_client.gui.order_review_system == 0) return;

	/* Do nothing for crashed vehicles */
	if (v->vehstatus & VS_CRASHED) return;

	/* Do nothing for stopped vehicles if setting is '1' */
	if (_settings_client.gui.order_review_system == 1 && (v->vehstatus & VS_STOPPED))
		return;

	/* do nothing we we're not the first vehicle in a share-chain */
	if (v->FirstShared() != v) return;

	/* Only check every 20 days, so that we don't flood the message log */
	if (v->owner == _local_company && v->day_counter % 20 == 0) {
		int n_st, problem_type = -1;
		const Order *order;
		int message = 0;

		/* Check the order list */
		n_st = 0;

		FOR_VEHICLE_ORDERS(v, order) {
			/* Dummy order? */
			if (order->IsType(OT_DUMMY)) {
				problem_type = 1;
				break;
			}
			/* Does station have a load-bay for this vehicle? */
			if (order->IsType(OT_GOTO_STATION)) {
				const Station *st = Station::Get(order->GetDestination());

				n_st++;
				if (!CanVehicleUseStation(v, st)) problem_type = 3;
			}
		}

		/* Check if the last and the first order are the same */
		if (v->GetNumOrders() > 1) {
			const Order *last = v->GetLastOrder();

			if (v->orders.list->GetFirstOrder()->Equals(*last)) {
				problem_type = 2;
			}
		}

		/* Do we only have 1 station in our order list? */
		if (n_st < 2 && problem_type == -1) problem_type = 0;

#ifndef NDEBUG
		if (v->orders.list != NULL) v->orders.list->DebugCheckSanity();
#endif

		/* We don't have a problem */
		if (problem_type < 0) return;

		message = STR_NEWS_VEHICLE_HAS_TOO_FEW_ORDERS + problem_type;
		//DEBUG(misc, 3, "Triggered News Item for vehicle %d", v->index);

		SetDParam(0, v->index);
		AddVehicleNewsItem(
			message,
			NS_ADVICE,
			v->index
		);
	}
}

/**
 * Removes an order from all vehicles. Triggers when, say, a station is removed.
 * @param type The type of the order (OT_GOTO_[STATION|DEPOT|WAYPOINT]).
 * @param destination The destination. Can be a StationID, DepotID or WaypointID.
 */
void RemoveOrderFromAllVehicles(OrderType type, DestinationID destination)
{
	Vehicle *v;

	/* Aircraft have StationIDs for depot orders and never use DepotIDs
	 * This fact is handled specially below
	 */

	/* Go through all vehicles */
	FOR_ALL_VEHICLES(v) {
		Order *order;

		order = &v->current_order;
		if ((v->type == VEH_AIRCRAFT && order->IsType(OT_GOTO_DEPOT) ? OT_GOTO_STATION : order->GetType()) == type &&
				v->current_order.GetDestination() == destination) {
			order->MakeDummy();
			SetWindowDirty(WC_VEHICLE_VIEW, v->index);
		}

		/* Clear the order from the order-list */
		int id = -1;
		FOR_VEHICLE_ORDERS(v, order) {
			id++;
			if (order->IsType(OT_GOTO_DEPOT) && (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0) continue;
			if ((v->type == VEH_AIRCRAFT && order->IsType(OT_GOTO_DEPOT) ? OT_GOTO_STATION : order->GetType()) == type &&
					order->GetDestination() == destination) {
				order->MakeDummy();
				for (const Vehicle *w = v->FirstShared(); w != NULL; w = w->NextShared()) {
					/* In GUI, simulate by removing the order and adding it back */
					InvalidateVehicleOrder(w, id | (INVALID_VEH_ORDER_ID << 8));
					InvalidateVehicleOrder(w, (INVALID_VEH_ORDER_ID << 8) | id);
				}
			}
		}
	}
}

/**
 *
 * Checks if a vehicle has a GOTO_DEPOT in his order list
 *
 * @return True if this is true (lol ;))
 *
 */
bool VehicleHasDepotOrders(const Vehicle *v)
{
	const Order *order;

	FOR_VEHICLE_ORDERS(v, order) {
		if (order->IsType(OT_GOTO_DEPOT))
			return true;
	}

	return false;
}

/**
 *
 * Delete all orders from a vehicle
 *
 */
void DeleteVehicleOrders(Vehicle *v, bool keep_orderlist)
{
	DeleteOrderWarnings(v);

	if (v->IsOrderListShared()) {
		/* Remove ourself from the shared order list. */
		v->RemoveFromShared();
		v->orders.list = NULL;
	} else if (v->orders.list != NULL) {
		/* Remove the orders */
		v->orders.list->FreeChain(keep_orderlist);
		if (!keep_orderlist) v->orders.list = NULL;
	}
}

uint16 GetServiceIntervalClamped(uint interval, CompanyID company_id)
{
	return (Company::Get(company_id)->settings.vehicle.servint_ispercent) ? Clamp(interval, MIN_SERVINT_PERCENT, MAX_SERVINT_PERCENT) : Clamp(interval, MIN_SERVINT_DAYS, MAX_SERVINT_DAYS);
}

/**
 *
 * Check if a vehicle has any valid orders
 *
 * @return false if there are no valid orders
 * @note Conditional orders are not considered valid destination orders
 *
 */
static bool CheckForValidOrders(const Vehicle *v)
{
	const Order *order;

	FOR_VEHICLE_ORDERS(v, order) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				return true;

			default:
				break;
		}
	}

	return false;
}

/**
 * Compare the variable and value based on the given comparator.
 */
static bool OrderConditionCompare(OrderConditionComparator occ, int variable, int value)
{
	switch (occ) {
		case OCC_EQUALS:      return variable == value;
		case OCC_NOT_EQUALS:  return variable != value;
		case OCC_LESS_THAN:   return variable <  value;
		case OCC_LESS_EQUALS: return variable <= value;
		case OCC_MORE_THAN:   return variable >  value;
		case OCC_MORE_EQUALS: return variable >= value;
		case OCC_IS_TRUE:     return variable != 0;
		case OCC_IS_FALSE:    return variable == 0;
		default: NOT_REACHED();
	}
}

/**
 * Process a conditional order and determine the next order.
 * @param order the order the vehicle currently has
 * @param v the vehicle to update
 * @return index of next order to jump to, or INVALID_VEH_ORDER_ID to use the next order
 */
VehicleOrderID ProcessConditionalOrder(const Order *order, const Vehicle *v)
{
	if (order->GetType() != OT_CONDITIONAL) return INVALID_VEH_ORDER_ID;

	bool skip_order = false;
	OrderConditionComparator occ = order->GetConditionComparator();
	uint16 value = order->GetConditionValue();

	switch (order->GetConditionVariable()) {
		case OCV_LOAD_PERCENTAGE:  skip_order = OrderConditionCompare(occ, CalcPercentVehicleFilled(v, NULL), value); break;
		case OCV_RELIABILITY:      skip_order = OrderConditionCompare(occ, ToPercent16(v->reliability),       value); break;
		case OCV_MAX_SPEED:        skip_order = OrderConditionCompare(occ, v->GetDisplayMaxSpeed() * 10 / 16, value); break;
		case OCV_AGE:              skip_order = OrderConditionCompare(occ, v->age / DAYS_IN_LEAP_YEAR,        value); break;
		case OCV_REQUIRES_SERVICE: skip_order = OrderConditionCompare(occ, v->NeedsServicing(),               value); break;
		case OCV_UNCONDITIONALLY:  skip_order = true; break;
		default: NOT_REACHED();
	}

	return skip_order ? order->GetConditionSkipToOrder() : (VehicleOrderID)INVALID_VEH_ORDER_ID;
}

/**
 * Update the vehicle's destination tile from an order.
 * @param order the order the vehicle currently has
 * @param v the vehicle to update
 * @param conditional_depth the depth (amount of steps) to go with conditional orders. This to prevent infinite loops.
 */
bool UpdateOrderDest(Vehicle *v, const Order *order, int conditional_depth)
{
	if (conditional_depth > v->GetNumOrders()) return false;

	switch (order->GetType()) {
		case OT_GOTO_STATION:
			v->dest_tile = v->GetOrderStationLocation(order->GetDestination());
			return true;

		case OT_GOTO_DEPOT:
			if (v->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
				/* We need to search for the nearest depot (hangar). */
				TileIndex location;
				DestinationID destination;
				bool reverse;

				if (v->FindClosestDepot(&location, &destination, &reverse)) {
					v->dest_tile = location;
					v->current_order.MakeGoToDepot(destination, v->current_order.GetDepotOrderType(), v->current_order.GetNonStopType(), (OrderDepotActionFlags)(v->current_order.GetDepotActionType() & ~ODATFB_NEAREST_DEPOT), v->current_order.GetRefitCargo(), v->current_order.GetRefitSubtype());

					/* If there is no depot in front, reverse automatically (trains only) */
					if (v->type == VEH_TRAIN && reverse) DoCommand(v->tile, v->index, 0, DC_EXEC, CMD_REVERSE_TRAIN_DIRECTION);

					if (v->type == VEH_AIRCRAFT) {
						Aircraft *a = Aircraft::From(v);
						if (a->state == FLYING && a->targetairport != destination) {
							/* The aircraft is now heading for a different hangar than the next in the orders */
							extern void AircraftNextAirportPos_and_Order(Aircraft *a);
							AircraftNextAirportPos_and_Order(a);
						}
					}
					return true;
				}

				UpdateVehicleTimetable(v, true);
				v->IncrementOrderIndex();
			} else if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !v->NeedsServicing()) {
				UpdateVehicleTimetable(v, true);
				v->IncrementOrderIndex();
			} else {
				if (v->type != VEH_AIRCRAFT) {
					v->dest_tile = Depot::Get(order->GetDestination())->xy;
				}
				return true;
			}
			break;

		case OT_GOTO_WAYPOINT:
			v->dest_tile = Waypoint::Get(order->GetDestination())->xy;
			return true;

		case OT_CONDITIONAL: {
			VehicleOrderID next_order = ProcessConditionalOrder(order, v);
			if (next_order != INVALID_VEH_ORDER_ID) {
				UpdateVehicleTimetable(v, false);
				v->cur_order_index = next_order;
				v->current_order_time += v->GetOrder(next_order)->travel_time;
			} else {
				UpdateVehicleTimetable(v, true);
				v->IncrementOrderIndex();
			}
			break;
		}

		default:
			v->dest_tile = 0;
			return false;
	}

	assert(v->cur_order_index < v->GetNumOrders());

	/* Get the current order */
	order = v->GetOrder(v->cur_order_index);
	v->current_order = *order;
	return UpdateOrderDest(v, order, conditional_depth + 1);
}

/**
 * Handle the orders of a vehicle and determine the next place
 * to go to if needed.
 * @param v the vehicle to do this for.
 * @return true *if* the vehicle is eligible for reversing
 *              (basically only when leaving a station).
 */
bool ProcessOrders(Vehicle *v)
{
	switch (v->current_order.GetType()) {
		case OT_GOTO_DEPOT:
			/* Let a depot order in the orderlist interrupt. */
			if (!(v->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) return false;
			break;

		case OT_LOADING:
			return false;

		case OT_LEAVESTATION:
			if (v->type != VEH_AIRCRAFT) return false;
			break;

		default: break;
	}

	/**
	 * Reversing because of order change is allowed only just after leaving a
	 * station (and the difficulty setting to allowed, of course)
	 * this can be detected because only after OT_LEAVESTATION, current_order
	 * will be reset to nothing. (That also happens if no order, but in that case
	 * it won't hit the point in code where may_reverse is checked)
	 */
	bool may_reverse = v->current_order.IsType(OT_NOTHING);

	/* Check if we've reached a non-stop station.. */
	if (((v->current_order.IsType(OT_GOTO_STATION) && (v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) || v->current_order.IsType(OT_GOTO_WAYPOINT)) &&
			IsTileType(v->tile, MP_STATION) &&
			v->current_order.GetDestination() == GetStationIndex(v->tile)) {
		if (v->current_order.IsType(OT_GOTO_STATION)) v->last_station_visited = v->current_order.GetDestination();
		UpdateVehicleTimetable(v, true);
		v->IncrementOrderIndex();
	}

	/* Get the current order */
	if (v->cur_order_index >= v->GetNumOrders()) v->cur_order_index = 0;

	const Order *order = v->GetOrder(v->cur_order_index);

	/* If no order, do nothing. */
	if (order == NULL || (v->type == VEH_AIRCRAFT && !CheckForValidOrders(v))) {
		if (v->type == VEH_AIRCRAFT) {
			/* Aircraft do something vastly different here, so handle separately */
			extern void HandleMissingAircraftOrders(Aircraft *v);
			HandleMissingAircraftOrders(Aircraft::From(v));
			return false;
		}

		v->current_order.Free();
		v->dest_tile = 0;
		return false;
	}

	/* If it is unchanged, keep it. */
	if (order->Equals(v->current_order) && (v->type == VEH_AIRCRAFT || v->dest_tile != 0) &&
			(v->type != VEH_SHIP || !order->IsType(OT_GOTO_STATION) || Station::Get(order->GetDestination())->dock_tile != INVALID_TILE)) {
		return false;
	}

	/* Otherwise set it, and determine the destination tile. */
	v->current_order = *order;

	InvalidateVehicleOrder(v, -2);
	switch (v->type) {
		default:
			NOT_REACHED();

		case VEH_ROAD:
		case VEH_TRAIN:
			break;

		case VEH_AIRCRAFT:
		case VEH_SHIP:
			SetWindowClassesDirty(GetWindowClassForVehicleType(v->type));
			break;
	}

	return UpdateOrderDest(v, order) && may_reverse;
}

/**
 * Check whether the given vehicle should stop at the given station
 * based on this order and the non-stop settings.
 * @param v       the vehicle that might be stopping.
 * @param station the station to stop at.
 * @return true if the vehicle should stop.
 */
bool Order::ShouldStopAtStation(const Vehicle *v, StationID station) const
{
	bool is_dest_station = this->IsType(OT_GOTO_STATION) && this->dest == station;
	return
			(!this->IsType(OT_GOTO_DEPOT) || (this->GetDepotOrderType() & ODTFB_PART_OF_ORDERS) != 0) &&
			v->last_station_visited != station && // Do stop only when we've not just been there
			/* Finally do stop when there is no non-stop flag set for this type of station. */
			!(this->GetNonStopType() & (is_dest_station ? ONSF_NO_STOP_AT_DESTINATION_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS));
}

void InitializeOrders()
{
	_order_pool.CleanPool();

	_orderlist_pool.CleanPool();

	_backup_orders_tile = 0;
}
