/* $Id$ */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "core/pool_func.hpp"
#include "economy_base.h"
#include "station_base.h"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

void InitializeCargoPackets()
{
	_cargopacket_pool.CleanPool();
}

CargoPacket::CargoPacket(StationID in_source, uint16 in_count, SourceType source_type, SourceID in_source_id) :
	count(in_count),
	source(in_source),
	source_id(in_source_id)
{
	if (Station::IsValidID(source)) {
		assert(count != 0);
		this->source_xy = Station::Get(source)->xy;
	}

	this->loaded_at_xy    = this->source_xy;

	this->source_type     = source_type;
}

/**
 * Invalidates (sets source_id to INVALID_SOURCE) all cargo packets from given source
 * @param src_type type of source
 * @param src index of source
 */
/* static */ void CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	CargoPacket *cp;
	FOR_ALL_CARGOPACKETS(cp) {
		if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
	}
}

/*
 *
 * Cargo list implementation
 *
 */

CargoList::~CargoList()
{
	while (!packets.empty()) {
		delete packets.front();
		packets.pop_front();
	}
}

void CargoList::AgeCargo()
{
	if (packets.empty()) return;

	days_in_transit = 0;
	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		CargoPacket *cp = *it;
		if (cp->days_in_transit != 0xFF) ++(cp->days_in_transit);
		days_in_transit += cp->days_in_transit * cp->count;
	}
}

void CargoList::RemoveFromCache(CargoPacket *cp) {
	this->count -= cp->count;
	this->feeder_share -= cp->feeder_share;
	this->days_in_transit -= cp->days_in_transit * cp->count;
}

void CargoList::AddToCache(CargoPacket *cp) {
	this->count += cp->count;
	this->feeder_share += cp->feeder_share;
	this->days_in_transit += cp->count * cp->days_in_transit;
}

void CargoList::Append(CargoPacket *cp, bool merge)
{
	assert(cp != NULL);

	if (merge) {
		for (List::iterator it = packets.begin(); it != packets.end(); it++) {
			CargoPacket *in_list = *it;
			if (in_list->SameSource(cp) && in_list->count + cp->count <= CargoPacket::MAX_COUNT) {
				in_list->count += cp->count;
				in_list->feeder_share += cp->feeder_share;
				AddToCache(cp);
				delete cp;
				return;
			}
		}
	}

	/* The packet could or should not be merged with another one */
	packets.push_back(cp);
	AddToCache(cp);
}


void CargoList::Truncate(uint max_remain)
{
	for (List::iterator it = packets.begin(); it != packets.end();) {
		CargoPacket * cp = *it;
		uint local_count = cp->count;
		if (max_remain == 0) {
			RemoveFromCache(cp);
			delete cp;
			packets.erase(it++);
		} else {
			if (local_count > max_remain) {
				uint diff = local_count - max_remain;
				this->count -= diff;
				this->days_in_transit -= cp->days_in_transit * diff;
				cp->count = max_remain;
				max_remain = 0;
			} else {
				max_remain -= local_count;
			}
			++it;
		}
	}
}

bool CargoList::MoveTo(CargoList *dest, uint max_move, CargoList::MoveToAction mta, CargoPayment *payment, uint data)
{
	assert(mta == MTA_FINAL_DELIVERY || dest != NULL);
	assert(mta == MTA_UNLOAD || mta == MTA_CARGO_LOAD || payment != NULL);

	List::iterator it = packets.begin();
	while(it != packets.end() && max_move > 0) {
		CargoPacket *cp = *it;
		if (cp->count <= max_move) {
			/* Can move the complete packet */
			if (cp->source == data && mta == MTA_FINAL_DELIVERY) {
				++it;
			} else {
				max_move -= cp->count;
				packets.erase(it++);
				RemoveFromCache(cp);
				switch(mta) {
					case MTA_FINAL_DELIVERY:
						payment->PayFinalDelivery(cp, cp->count);
						delete cp;
						continue; // of the loop

					case MTA_CARGO_LOAD:
						cp->loaded_at_xy = data;
						break;

					case MTA_TRANSFER:
						/* here the packet isn't in either list, so we can change it */
						cp->feeder_share += payment->PayTransfer(cp, cp->count);
						break;

					case MTA_UNLOAD:
						break;
				}
				dest->Append(cp, false);
			}
		} else {
			/* Can move only part of the packet, so split it into two pieces */
			if (mta != MTA_FINAL_DELIVERY) {
				Money fs = cp->feeder_share * max_move / static_cast<uint>(cp->count);
				cp->feeder_share -= fs;
				CargoPacket *cp_new = new CargoPacket(max_move, cp->days_in_transit, fs);

				cp_new->source          = cp->source;
				cp_new->source_xy       = cp->source_xy;
				cp_new->loaded_at_xy    = (mta == MTA_CARGO_LOAD) ? data : cp->loaded_at_xy;

				cp_new->source_type     = cp->source_type;
				cp_new->source_id       = cp->source_id;

				RemoveFromCache(cp_new); // this reflects the changes in cp

				if (mta == MTA_TRANSFER) {
					/* add the feeder share before inserting in dest */
					cp_new->feeder_share += payment->PayTransfer(cp_new, max_move);
				}

				dest->Append(cp_new, false);
			} else if (cp->source == data) {
				++it;
				continue;
			} else {
				payment->PayFinalDelivery(cp, max_move);
				this->count -= max_move;
				this->days_in_transit -= max_move * cp->days_in_transit;
			}
			cp->count -= max_move;

			max_move = 0;
		}
	}

	return it != packets.end();
}

void CargoList::InvalidateCache()
{
	count = 0;
	feeder_share = 0;
	days_in_transit = 0;

	if (packets.empty()) return;

	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		CargoPacket *cp = *it;
		count           += cp->count;
		days_in_transit += cp->days_in_transit * cp->count;
		feeder_share    += cp->feeder_share;
	}
}

