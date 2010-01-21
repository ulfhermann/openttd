/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_func.h Functions related to orders. */

#ifndef ORDER_FUNC_H
#define ORDER_FUNC_H

#include "order_type.h"
#include "vehicle_type.h"
#include "tile_type.h"
#include "group_type.h"
#include "company_type.h"

struct BackuppedOrders {
	BackuppedOrders() : order(NULL), name(NULL) { }
	~BackuppedOrders() { free(order); free(name); }

	VehicleID clone;
	VehicleOrderID orderindex;
	GroupID group;
	Order *order;
	uint16 service_interval;
	char *name;
};

extern TileIndex _backup_orders_tile;
extern BackuppedOrders _backup_orders_data;

void BackupVehicleOrders(const Vehicle *v, BackuppedOrders *order = &_backup_orders_data);
void RestoreVehicleOrders(const Vehicle *v, const BackuppedOrders *order = &_backup_orders_data);

/* Functions */
void RemoveOrderFromAllVehicles(OrderType type, DestinationID destination);
void InvalidateVehicleOrder(const Vehicle *v, int data);
bool VehicleHasDepotOrders(const Vehicle *v);
void CheckOrders(const Vehicle*);
void DeleteVehicleOrders(Vehicle *v, bool keep_orderlist = false);
bool ProcessOrders(Vehicle *v);
bool UpdateOrderDest(Vehicle *v, const Order *order, int conditional_depth = 0);
VehicleOrderID ProcessConditionalOrder(const Order *order, const Vehicle *v);

void DrawOrderString(const Vehicle *v, const Order *order, int order_index, int y, bool selected, bool timetable, int left, int middle, int right);

#define MIN_SERVINT_PERCENT  5
#define MAX_SERVINT_PERCENT 90
#define MIN_SERVINT_DAYS    30
#define MAX_SERVINT_DAYS   800

/**
 * Clamp the service interval to the correct min/max. The actual min/max values
 * depend on whether it's in percent or days.
 * @param interval proposed service interval
 * @param company_id the owner of the vehicle
 * @return Clamped service interval
 */
uint16 GetServiceIntervalClamped(uint interval, CompanyID company_id);

#endif /* ORDER_FUNC_H */
