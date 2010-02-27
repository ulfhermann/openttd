/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_base.h Base class for orders. */

#ifndef ORDER_BASE_H
#define ORDER_BASE_H

#include "order_type.h"
#include "core/pool_type.hpp"
#include "core/bitmath_func.hpp"
#include "cargo_type.h"
#include "depot_type.h"
#include "station_type.h"
#include "vehicle_type.h"
#include "date_type.h"

typedef Pool<Order, OrderID, 256, 64000> OrderPool;
typedef Pool<OrderList, OrderListID, 128, 64000> OrderListPool;
extern OrderPool _order_pool;
extern OrderListPool _orderlist_pool;

/* If you change this, keep in mind that it is saved on 3 places:
 * - Load_ORDR, all the global orders
 * - Vehicle -> current_order
 * - REF_ORDER (all REFs are currently limited to 16 bits!!)
 */
struct Order : OrderPool::PoolItem<&_order_pool> {
private:
	friend const struct SaveLoad *GetVehicleDescription(VehicleType vt); ///< Saving and loading the current order of vehicles.
	friend void Load_VEHS();                                             ///< Loading of ancient vehicles.
	friend const struct SaveLoad *GetOrderDescription();                 ///< Saving and loading of orders.

	uint8 type;           ///< The type of order + non-stop flags
	uint8 flags;          ///< Load/unload types, depot order/action types.
	DestinationID dest;   ///< The destination of the order.

	CargoID refit_cargo;  ///< Refit CargoID
	byte refit_subtype;   ///< Refit subtype

public:
	Order *next;          ///< Pointer to next order. If NULL, end of list

	uint16 wait_time;    ///< How long in ticks to wait at the destination.
	uint16 travel_time;  ///< How long in ticks the journey to this destination should take.

	Order() : refit_cargo(CT_NO_REFIT) {}
	~Order() {}

	/**
	 * Create an order based on a packed representation of that order.
	 * @param packed the packed representation.
	 */
	Order(uint32 packed);

	/**
	 * Check whether this order is of the given type.
	 * @param type the type to check against.
	 * @return true if the order matches.
	 */
	inline bool IsType(OrderType type) const { return this->GetType() == type; }

	/**
	 * Get the type of order of this order.
	 * @return the order type.
	 */
	inline OrderType GetType() const { return (OrderType)GB(this->type, 0, 4); }

	/**
	 * 'Free' the order
	 * @note ONLY use on "current_order" vehicle orders!
	 */
	void Free();

	/**
	 * Makes this order a Go To Station order.
	 * @param destination the station to go to.
	 */
	void MakeGoToStation(StationID destination);

	/**
	 * Makes this order a Go To Depot order.
	 * @param destination   the depot to go to.
	 * @param order         is this order a 'default' order, or an overriden vehicle order?
	 * @param non_stop_type how to get to the depot?
	 * @param action        what to do in the depot?
	 * @param cargo         the cargo type to change to.
	 * @param subtype       the subtype to change to.
	 */
	void MakeGoToDepot(DepotID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type = ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, OrderDepotActionFlags action = ODATF_SERVICE_ONLY, CargoID cargo = CT_NO_REFIT, byte subtype = 0);

	/**
	 * Makes this order a Go To Waypoint order.
	 * @param destination the waypoint to go to.
	 */
	void MakeGoToWaypoint(StationID destination);

	/**
	 * Makes this order a Loading order.
	 * @param ordered is this an ordered stop?
	 */
	void MakeLoading(bool ordered);

	/**
	 * Makes this order a Leave Station order.
	 */
	void MakeLeaveStation();

	/**
	 * Makes this order a Dummy order.
	 */
	void MakeDummy();

	/**
	 * Makes this order an conditional order.
	 * @param order the order to jump to.
	 */
	void MakeConditional(VehicleOrderID order);

	/**
	 * Gets the destination of this order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
	 * @return the destination of the order.
	 */
	inline DestinationID GetDestination() const { return this->dest; }

	/**
	 * Sets the destination of this order.
	 * @param destination the new destination of the order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
	 */
	inline void SetDestination(DestinationID destination) { this->dest = destination; }

	/**
	 * Is this order a refit order.
	 * @pre IsType(OT_GOTO_DEPOT)
	 * @return true if a refit should happen.
	 */
	inline bool IsRefit() const { return this->refit_cargo < NUM_CARGO; }

	/**
	 * Get the cargo to to refit to.
	 * @pre IsType(OT_GOTO_DEPOT)
	 * @return the cargo type.
	 */
	inline CargoID GetRefitCargo() const { return this->refit_cargo; }

	/**
	 * Get the cargo subtype to to refit to.
	 * @pre IsType(OT_GOTO_DEPOT)
	 * @return the cargo subtype.
	 */
	inline byte GetRefitSubtype() const { return this->refit_subtype; }

	/**
	 * Make this depot order also a refit order.
	 * @param cargo   the cargo type to change to.
	 * @param subtype the subtype to change to.
	 * @pre IsType(OT_GOTO_DEPOT).
	 */
	void SetRefit(CargoID cargo, byte subtype = 0);

	/** How must the consist be loaded? */
	inline OrderLoadFlags GetLoadType() const { return (OrderLoadFlags)GB(this->flags, 4, 4); }
	/** How must the consist be unloaded? */
	inline OrderUnloadFlags GetUnloadType() const { return (OrderUnloadFlags)GB(this->flags, 0, 4); }
	/** At which stations must we stop? */
	inline OrderNonStopFlags GetNonStopType() const { return (OrderNonStopFlags)GB(this->type, 6, 2); }
	/** Where must we stop at the platform? */
	inline OrderStopLocation GetStopLocation() const { return (OrderStopLocation)GB(this->type, 4, 2); }
	/** What caused us going to the depot? */
	inline OrderDepotTypeFlags GetDepotOrderType() const { return (OrderDepotTypeFlags)GB(this->flags, 0, 4); }
	/** What are we going to do when in the depot. */
	inline OrderDepotActionFlags GetDepotActionType() const { return (OrderDepotActionFlags)GB(this->flags, 4, 4); }
	/** What variable do we have to compare? */
	inline OrderConditionVariable GetConditionVariable() const { return (OrderConditionVariable)GB(this->dest, 11, 5); }
	/** What is the comparator to use? */
	inline OrderConditionComparator GetConditionComparator() const { return (OrderConditionComparator)GB(this->type, 5, 3); }
	/** Get the order to skip to. */
	inline VehicleOrderID GetConditionSkipToOrder() const { return this->flags; }
	/** Get the value to base the skip on. */
	inline uint16 GetConditionValue() const { return GB(this->dest, 0, 11); }

	/** Set how the consist must be loaded. */
	inline void SetLoadType(OrderLoadFlags load_type) { SB(this->flags, 4, 4, load_type); }
	/** Set how the consist must be unloaded. */
	inline void SetUnloadType(OrderUnloadFlags unload_type) { SB(this->flags, 0, 4, unload_type); }
	/** Set whether we must stop at stations or not. */
	inline void SetNonStopType(OrderNonStopFlags non_stop_type) { SB(this->type, 6, 2, non_stop_type); }
	/** Set where we must stop at the platform. */
	inline void SetStopLocation(OrderStopLocation stop_location) { SB(this->type, 4, 2, stop_location); }
	/** Set the cause to go to the depot. */
	inline void SetDepotOrderType(OrderDepotTypeFlags depot_order_type) { SB(this->flags, 0, 4, depot_order_type); }
	/** Set what we are going to do in the depot. */
	inline void SetDepotActionType(OrderDepotActionFlags depot_service_type) { SB(this->flags, 4, 4, depot_service_type); }
	/** Set variable we have to compare. */
	inline void SetConditionVariable(OrderConditionVariable condition_variable) { SB(this->dest, 11, 5, condition_variable); }
	/** Set the comparator to use. */
	inline void SetConditionComparator(OrderConditionComparator condition_comparator) { SB(this->type, 5, 3, condition_comparator); }
	/** Get the order to skip to. */
	inline void SetConditionSkipToOrder(VehicleOrderID order_id) { this->flags = order_id; }
	/** Set the value to base the skip on. */
	inline void SetConditionValue(uint16 value) { SB(this->dest, 0, 11, value); }

	bool ShouldStopAtStation(const Vehicle *v, StationID station) const;
	TileIndex GetLocation(const Vehicle *v) const;

	/** Checks if this order has travel_time and if needed wait_time set. */
	inline bool IsCompletelyTimetabled() const
	{
		if (this->travel_time == 0 && !this->IsType(OT_CONDITIONAL)) return false;
		if (this->wait_time == 0 && this->IsType(OT_GOTO_STATION) && !(this->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) return false;
		return true;
	}

	/**
	 * Assign the given order to this one.
	 * @param other the data to copy (except next pointer).
	 */
	void AssignOrder(const Order &other);

	/**
	 * Does this order have the same type, flags and destination?
	 * @param other the second order to compare to.
	 * @return true if the type, flags and destination match.
	 */
	bool Equals(const Order &other) const;

	/**
	 * Pack this order into a 32 bits integer, or actually only
	 * the type, flags and destination.
	 * @return the packed representation.
	 * @note unpacking is done in the constructor.
	 */
	uint32 Pack() const;

	/**
	 * Pack this order into a 16 bits integer as close to the TTD
	 * representation as possible.
	 * @return the TTD-like packed representation.
	 */
	uint16 MapOldOrder() const;

	/**
	 * Converts this order from an old savegame's version;
	 * it moves all bits to the new location.
	 */
	void ConvertFromOldSavegame();
};

/** Shared order list linking together the linked list of orders and the list
 *  of vehicles sharing this order list.
 */
struct OrderList : OrderListPool::PoolItem<&_orderlist_pool> {
private:
	friend void AfterLoadVehicles(bool part_of_load); ///< For instantiating the shared vehicle chain
	friend const struct SaveLoad *GetOrderListDescription(); ///< Saving and loading of order lists.

	Order *first;                   ///< First order of the order list
	VehicleOrderID num_orders;      ///< NOSAVE: How many orders there are in the list
	uint num_vehicles;              ///< NOSAVE: Number of vehicles that share this order list
	Vehicle *first_shared;          ///< NOSAVE: pointer to the first vehicle in the shared order chain

	Ticks timetable_duration;       ///< NOSAVE: Total duration of the order list

public:
	/** Default constructor producing an invalid order list. */
	OrderList(VehicleOrderID num_orders = INVALID_VEH_ORDER_ID)
		: first(NULL), num_orders(num_orders), num_vehicles(0), first_shared(NULL),
		  timetable_duration(0) { }

	/** Create an order list with the given order chain for the given vehicle.
	 *  @param chain pointer to the first order of the order chain
	 *  @param v any vehicle using this orderlist
	 */
	OrderList(Order *chain, Vehicle *v) { this->Initialize(chain, v); }

	/** Destructor. Invalidates OrderList for re-usage by the pool. */
	~OrderList() {}

	/**
	 * Recomputes everything.
	 * @param chain first order in the chain
	 * @param v one of vehicle that is using this orderlist
	 */
	void Initialize(Order *chain, Vehicle *v);

	/**
	 * Get the first order of the order chain.
	 * @return the first order of the chain.
	 */
	inline Order *GetFirstOrder() const { return this->first; }

	/**
	 * Get a certain order of the order chain.
	 * @param index zero-based index of the order within the chain.
	 * @return the order at position index.
	 */
	Order *GetOrderAt(int index) const;

	/**
	 * Get the last order of the order chain.
	 * @return the last order of the chain.
	 */
	inline Order *GetLastOrder() const { return this->GetOrderAt(this->num_orders - 1); }

	/**
	 * Get number of orders in the order list.
	 * @return number of orders in the chain. */
	inline VehicleOrderID GetNumOrders() const { return this->num_orders; }

	/**
	 * Insert a new order into the order chain.
	 * @param new_order is the order to insert into the chain.
	 * @param index is the position where the order is supposed to be inserted. */
	void InsertOrderAt(Order *new_order, int index);

	/**
	 * Remove an order from the order list and delete it.
	 * @param index is the position of the order which is to be deleted.
	 */
	void DeleteOrderAt(int index);

	/**
	 * Move an order to another position within the order list.
	 * @param from is the zero-based position of the order to move.
	 * @param to is the zero-based position where the order is moved to. */
	void MoveOrder(int from, int to);

	/**
	 * Is this a shared order list?
	 * @return whether this order list is shared among multiple vehicles
	 */
	inline bool IsShared() const { return this->num_vehicles > 1; };

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *GetFirstSharedVehicle() const { return this->first_shared; }

	/**
	 * Return the number of vehicles that share this orders list
	 * @return the count of vehicles that use this shared orders list
	 */
	inline uint GetNumVehicles() const { return this->num_vehicles; }

	/**
	 * Checks whether a vehicle is part of the shared vehicle chain.
	 * @param v is the vehicle to search in the shared vehicle chain.
	 */
	bool IsVehicleInSharedOrdersList(const Vehicle *v) const;

	/**
	 * Gets the position of the given vehicle within the shared order vehicle list.
	 * @param v is the vehicle of which to get the position
	 * @return position of v within the shared vehicle chain.
	 */
	int GetPositionInSharedOrderList(const Vehicle *v) const;

	/**
	 * Adds the given vehicle to this shared order list.
	 * @note This is supposed to be called after the vehicle has been inserted
	 *       into the shared vehicle chain.
	 * @param v vehicle to add to the list
	 */
	inline void AddVehicle(Vehicle *v) { ++this->num_vehicles; }

	/**
	 * Removes the vehicle from the shared order list.
	 * @note This is supposed to be called when the vehicle is still in the chain
	 * @param v vehicle to remove from the list
	 */
	void RemoveVehicle(Vehicle *v);

	/**
	 * Checks whether all orders of the list have a filled timetable.
	 * @return whether all orders have a filled timetable.
	 */
	bool IsCompleteTimetable() const;

	/**
	 * Gets the total duration of the vehicles timetable or INVALID_TICKS is the timetable is not complete.
	 * @return total timetable duration or INVALID_TICKS for incomplete timetables
	 */
	inline Ticks GetTimetableTotalDuration() const { return this->IsCompleteTimetable() ? this->timetable_duration : INVALID_TICKS; }

	/**
	 * Gets the known duration of the vehicles timetable even if the timetable is not complete.
	 * @return known timetable duration
	 */
	inline Ticks GetTimetableDurationIncomplete() const { return this->timetable_duration; }

	/**
	 * Must be called if an order's timetable is changed to update internal book keeping.
	 * @param delta By how many ticks has the timetable duration changed
	 */
	void UpdateOrderTimetable(Ticks delta) { this->timetable_duration += delta; }

	/**
	 * Free a complete order chain.
	 * @param keep_orderlist If this is true only delete the orders, otherwise also delete the OrderList.
	 * @note do not use on "current_order" vehicle orders!
	 */
	void FreeChain(bool keep_orderlist = false);

	/**
	 * Checks for internal consistency of order list. Triggers assertion if something is wrong.
	 */
	void DebugCheckSanity() const;
};

#define FOR_ALL_ORDERS_FROM(var, start) FOR_ALL_ITEMS_FROM(Order, order_index, var, start)
#define FOR_ALL_ORDERS(var) FOR_ALL_ORDERS_FROM(var, 0)


#define FOR_VEHICLE_ORDERS(v, order) for (order = (v->orders.list == NULL) ? NULL : v->orders.list->GetFirstOrder(); order != NULL; order = order->next)


#define FOR_ALL_ORDER_LISTS_FROM(var, start) FOR_ALL_ITEMS_FROM(OrderList, orderlist_index, var, start)
#define FOR_ALL_ORDER_LISTS(var) FOR_ALL_ORDER_LISTS_FROM(var, 0)

#endif /* ORDER_H */
