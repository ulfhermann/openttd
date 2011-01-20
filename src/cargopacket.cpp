/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket.cpp Implementation of the cargo packets. */

#include "stdafx.h"
#include "station_base.h"
#include "core/pool_func.hpp"
#include "core/random_func.hpp"
#include "economy_base.h"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

/**
 * Initialize, i.e. clean, the pool with cargo packets.
 */
void InitializeCargoPackets()
{
	_cargopacket_pool.CleanPool();
}

/**
 * Create a new packet for savegame loading.
 */
CargoPacket::CargoPacket()
{
	this->source_type = ST_INDUSTRY;
	this->source_id   = INVALID_SOURCE;
}

/**
 * Creates a new cargo packet.
 * @param source      Source station of the packet.
 * @param source_xy   Source location of the packet.
 * @param count       Number of cargo entities to put in this packet.
 * @param source_type 'Type' of source the packet comes from (for subsidies).
 * @param source_id   Actual source of the packet (for subsidies).
 * @pre count != 0
 * @note We have to zero memory ourselves here because we are using a 'new'
 * that, in contrary to all other pools, does not memset to 0.
 */
CargoPacket::CargoPacket(StationID source, TileIndex source_xy, uint16 count, SourceType source_type, SourceID source_id) :
	feeder_share(0),
	count(count),
	days_in_transit(0),
	source_id(source_id),
	source(source),
	source_xy(source_xy),
	loaded_at_xy(0)
{
	assert(count != 0);
	this->source_type  = source_type;
}

/**
 * Creates a new cargo packet. Initializes the fields that cannot be changed later.
 * Used when loading or splitting packets.
 * @param count           Number of cargo entities to put in this packet.
 * @param days_in_transit Number of days the cargo has been in transit.
 * @param source          Station the cargo was initially loaded.
 * @param source_xy       Station location the cargo was initially loaded.
 * @param loaded_at_xy    Location the cargo was loaded last.
 * @param feeder_share    Feeder share the packet has already accumulated.
 * @param source_type     'Type' of source the packet comes from (for subsidies).
 * @param source_id       Actual source of the packet (for subsidies).
 * @note We have to zero memory ourselves here because we are using a 'new'
 * that, in contrary to all other pools, does not memset to 0.
 */
CargoPacket::CargoPacket(uint16 count, byte days_in_transit, StationID source, TileIndex source_xy, TileIndex loaded_at_xy, Money feeder_share, SourceType source_type, SourceID source_id) :
		feeder_share(feeder_share),
		count(count),
		days_in_transit(days_in_transit),
		source_id(source_id),
		source(source),
		source_xy(source_xy),
		loaded_at_xy(loaded_at_xy)
{
	assert(count != 0);
	this->source_type = source_type;
}

/**
 * Split this packet in two and return the split off part.
 * @param new_size Size of the remaining part.
 * @return Split off part.
 */
FORCEINLINE CargoPacket *CargoPacket::Split(uint new_size)
{
	Money fs = this->feeder_share * new_size / static_cast<uint>(this->count);
	CargoPacket *cp_new = new CargoPacket(new_size, this->days_in_transit, this->source, this->source_xy, this->loaded_at_xy, fs, this->source_type, this->source_id);
	this->feeder_share -= fs;
	this->count -= new_size;
	return cp_new;
}

/**
 * Merge another packet into this one.
 * @param cp Packet to be merged in.
 */
FORCEINLINE void CargoPacket::Merge(CargoPacket *cp)
{
	this->count += cp->count;
	this->feeder_share += cp->feeder_share;
	delete cp;
}

/**
 * Invalidates (sets source_id to INVALID_SOURCE) all cargo packets from given source.
 * @param src_type Type of source.
 * @param src Index of source.
 */
/* static */ void CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	CargoPacket *cp;
	FOR_ALL_CARGOPACKETS(cp) {
		if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
	}
}

/**
 * Invalidates (sets source to INVALID_STATION) all cargo packets from given station.
 * @param sid Station that gets removed.
 */
/* static */ void CargoPacket::InvalidateAllFrom(StationID sid)
{
	CargoPacket *cp;
	FOR_ALL_CARGOPACKETS(cp) {
		if (cp->source == sid) cp->source = INVALID_STATION;
	}
}

/*
 *
 * Cargo list implementation
 *
 */

/**
 * Destroy the cargolist ("frees" all cargo packets).
 */
template <class Tinst, class Tcont>
CargoList<Tinst, Tcont>::~CargoList()
{
	for (Iterator it(this->packets.begin()); it != this->packets.end(); ++it) {
		delete *it;
	}
}

/**
 * Update the cached values to reflect the removal of this packet.
 * Decreases count and days_in_transit.
 * @param cp Packet to be removed from cache.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::RemoveFromCache(const CargoPacket *cp)
{
	this->count                 -= cp->count;
	this->cargo_days_in_transit -= cp->days_in_transit * cp->count;
}

/**
 * Update the cache to reflect adding of this packet.
 * Increases count and days_in_transit.
 * @param cp New packet to be inserted.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->count;
	this->cargo_days_in_transit += cp->days_in_transit * cp->count;
}

/**
 * Appends the given cargo packet. Tries to merge it with another one in the
 * packets list. If no fitting packet is found, appends it.
 * @warning After appending this packet may not exist anymore!
 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
 * @param cp Cargo packet to add.
 * @param update_cache If false, the cache is not updated; used when loading from
 *        the reservation list.
 * @pre cp != NULL
 */
void VehicleCargoList::Append(CargoPacket *cp, bool update_cache)
{
	assert(cp != NULL);
	if (update_cache) this->AddToCache(cp);
	for (CargoPacketList::reverse_iterator it(this->packets.rbegin()); it != this->packets.rend(); it++) {
		CargoPacket *icp = *it;
		if (VehicleCargoList::AreMergable(icp, cp) && icp->count + cp->count <= CargoPacket::MAX_COUNT) {
			icp->Merge(cp);
			return;
		}
	}

	/* The packet could not be merged with another one */
	this->packets.push_back(cp);
}

/**
 * Truncates the cargo in this list to the given amount. It leaves the
 * first count cargo entities and removes the rest.
 * @param max_remaining Maximum amount of entities to be in the list after the command.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::Truncate(uint max_remaining)
{
	for (Iterator it(packets.begin()); it != packets.end(); /* done during loop*/) {
		CargoPacket *cp = *it;
		if (max_remaining == 0) {
			/* Nothing should remain, just remove the packets. */
			this->packets.erase(it++);
			static_cast<Tinst *>(this)->RemoveFromCache(cp);
			delete cp;
			continue;
		}

		uint local_count = cp->count;
		if (local_count > max_remaining) {
			uint diff = local_count - max_remaining;
			this->count -= diff;
			this->cargo_days_in_transit -= cp->days_in_transit * diff;
			cp->count = max_remaining;
			max_remaining = 0;
		} else {
			max_remaining -= local_count;
		}
		++it;
	}
}

/**
 * Reserves a packet for later loading and adds it to the cache.
 * @param cp Packet to be reserved.
 */
void VehicleCargoList::Reserve(CargoPacket *cp)
{
	assert(cp != NULL);
	this->AddToCache(cp);
	this->reserved_count += cp->count;
	this->reserved.push_back(cp);
}

/**
 * Returns all reserved cargo to the station and removes it from the cache.
 * @param ID of next the station the cargo wants to go next.
 * @param dest Station the cargo is returned to.
 */
void VehicleCargoList::Unreserve(StationID next, StationCargoList *dest)
{
	Iterator it(this->reserved.begin());
	while (it != this->reserved.end()) {
		CargoPacket *cp = *it;
		this->RemoveFromCache(cp);
		this->reserved_count -= cp->count;
		dest->Append(next, cp);
		this->reserved.erase(it++);
	}
}

/**
 * Load packets from the reservation list.
 * @params max_move Number of cargo to load.
 * @return Amount of cargo actually loaded.
 */
uint VehicleCargoList::LoadReserved(uint max_move)
{
	uint orig_max = max_move;
	Iterator it(this->reserved.begin());
	while (it != this->reserved.end() && max_move > 0) {
		CargoPacket *cp = *it;
		if (cp->count <= max_move) {
			/* Can move the complete packet */
			max_move -= cp->count;
			this->reserved.erase(it++);
			this->reserved_count -= cp->count;
			this->Append(cp, false);
		} else {
			cp->count -= max_move;
			CargoPacket *cp_new = new CargoPacket(max_move, cp->days_in_transit, cp->source, cp->source_xy, cp->loaded_at_xy, 0, cp->source_type, cp->source_id);
			this->Append(cp_new, false);
			this->reserved_count -= max_move;
			max_move = 0;
		}
	}
	return orig_max - max_move;
}

/**
 * Move a single packet or part of it from this list to a vehicle and increment
 * the given iterator.
 * @param dest       Vehicle cargo list to move to.
 * @param it         Iterator pointing to the packet.
 * @param cap        Maximum amount of cargo to be moved.
 * @param load_place New loaded_at for the packet.
 * @param reserve    If the packet should be loaded on or reserved for the vehicle.
 * @return           Actual amount of cargo which has been moved.
 */
template<class Tinst, class Tcont>
uint CargoList<Tinst, Tcont>::MovePacket(VehicleCargoList *dest, Iterator &it, uint cap, TileIndex load_place, bool reserve)
{
	CargoPacket *packet = this->RemovePacket(it, cap, load_place);
	uint ret = packet->count;
	if (reserve) {
		dest->Reserve(packet);
	} else {
		dest->Append(packet);
	}
	return ret;
}

/**
 * Move a single packet or part of it from this list to a station and increment
 * the given iterator.
 * @param dest Station cargo list to move to.
 * @param next Next station the packet will travel to.
 * @param it Iterator pointing to the packet.
 * @param cap Maximum amount of cargo to be moved.
 * @return Actual amount of cargo which has been moved.
 */
template<class Tinst, class Tcont>
uint CargoList<Tinst, Tcont>::MovePacket(StationCargoList *dest, StationID next, Iterator &it, uint cap)
{
	CargoPacket *packet = this->RemovePacket(it, cap);
	uint ret = packet->count;
	dest->Append(next, packet);
	return ret;
}

/**
 * Remove a single packet or part of it from this list and increment the given
 * iterator.
 * @param it Iterator pointing to the packet.
 * @param cap Maximum amount of cargo to be moved.
 * @param load_place New loaded_at for the packet or INVALID_TILE if the current
 *        one shall be kept.
 * @return Removed packet.
 */
template<class Tinst, class Tcont>
CargoPacket *CargoList<Tinst, Tcont>::RemovePacket(Iterator &it, uint cap, TileIndex load_place)
{
	CargoPacket *packet = *it;
	/* load the packet if possible */
	if (packet->count > cap) {
		/* packet needs to be split */
		packet = packet->Split(cap);
		assert(packet->count == cap);
		++it;
	} else {
		this->packets.erase(it++);
	}
	static_cast<Tinst *>(this)->RemoveFromCache(packet);
	if (load_place != INVALID_TILE) {
		packet->loaded_at_xy = load_place;
	}
	return packet;
}

/**
 * Invalidates the cached data and rebuilds it.
 */
template <class Tinst, class Tcont>
void CargoList<Tinst, Tcont>::InvalidateCache()
{
	this->count = 0;
	this->cargo_days_in_transit = 0;

	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		static_cast<Tinst *>(this)->AddToCache(*it);
	}
}

/**
 * Delete a vehicle cargo list and clear its reservation list.
 */
VehicleCargoList::~VehicleCargoList()
{
	for (Iterator it(this->reserved.begin()); it != this->reserved.end(); ++it) {
		delete *it;
	}
}

/**
 * Deliver a specific packet or part of it to a station and handle payment. The
 * given iterator is incremented in the process.
 * @param it      Iterator pointing to the packet to be delivered.
 * @param cap     Maximum amount of cargo to be unloaded.
 * @param payment Payment object to use for payment.
 * @return        Amount of cargo actually unloaded.
 */
uint VehicleCargoList::DeliverPacket(Iterator &it, uint cap, CargoPayment *payment)
{
	CargoPacket *p = *it;
	uint unloaded = 0;
	if (p->count <= cap) {
		payment->PayFinalDelivery(p, p->count);
		this->packets.erase(it++);
		this->RemoveFromCache(p);
		unloaded = p->count;
		delete p;
	} else {
		payment->PayFinalDelivery(p, cap);
		this->count -= cap;
		this->cargo_days_in_transit -= cap * p->days_in_transit;
		this->feeder_share -= p->feeder_share;
		p->feeder_share = 0;
		p->count -= cap;
		unloaded = cap;
		++it;
	}
	return unloaded;
}

/**
 * Keep a packet in the vehicle while unloading by temporarily moving it to the
 * reservation list. The given iterator is incremented in the process.
 * @param it Iterator pointing to the packet.
 * @return Size of the packet.
 */
uint VehicleCargoList::KeepPacket(Iterator &it)
{
	CargoPacket *cp = *it;
	this->reserved.push_back(cp);
	this->reserved_count += cp->count;
	this->packets.erase(it++);
	return cp->count;
}

/**
 * Transfer a packet to a station, but don't deliver it. Increment the given
 * iterator in the process.
 * @param it Iterator pointing to a packet in the list.
 * @param cap Maximum amount of cargo to be transferred.
 * @param dest Cargo list of the station the cargo should be transferred to.
 * @param payment Payment object to be updated with the resulting transfer
 *                credits.
 * @param next ID of the station the cargo wants to go to next.
 * @return Amount of cargo actually moved.
 */
uint VehicleCargoList::TransferPacket(Iterator &it, uint cap, StationCargoList *dest, CargoPayment *payment, StationID next)
{
	CargoPacket *cp = this->RemovePacket(it, cap);
	cp->feeder_share += payment->PayTransfer(cp, cp->count);
	uint ret = cp->count;
	dest->Append(next, cp);
	return ret;
}

/**
 * Determine what a cargo packet arriving at the station this list belongs to
 * will do, using the "old", non-cargodist algorithm.
 * @param flags  Unload flags telling if the cargo is accepted and what order
 *               flags there are.
 * @param source ID of the packets source station.
 * @return       Unload type (deliver, transfer, keep) telling what to do with
 *               the packet.
 */
UnloadType StationCargoList::WillUnloadOld(byte flags, StationID source)
{
	/* try to unload cargo */
	bool move = (flags & (UL_DELIVER | UL_ACCEPTED | UL_TRANSFER)) != 0;
	/* try to deliver cargo if unloading */
	bool deliver = (flags & UL_ACCEPTED) && !(flags & UL_TRANSFER) && (source != this->station->index);
	/* transfer cargo if delivery was unsuccessful */
	bool transfer = (flags & (UL_TRANSFER | UL_DELIVER)) != 0;
	if (move) {
		if(deliver) {
			return UL_DELIVER;
		} else if (transfer) {
			return UL_TRANSFER;
		} else {
			/* this case is for (non-)delivery to the source station without special flags.
			 * like the code in MoveTo did, we keep the packet in this case
			 */
			return UL_KEEP;
		}
	} else {
		return UL_KEEP;
	}
}

/**
 * Determine what a cargo packet arriving at the station this list belongs to
 * will do, using the Cargodist algorithm.
 * @param flags  Unload flags telling if the cargo is accepted and what order
 *               flags there are.
 * @param next   Station the vehicle the cargo is coming from will
 *               visit next (or INVALID_STATION if unknown).
 * @param via    Station the cargo wants to go to next. If that is this
 *               station the cargo wants to be delivered.
 * @param source ID of the packets source station.
 * @return       Unload type (deliver, transfer, keep) telling what to do with
 *               the packet.
 */
UnloadType StationCargoList::WillUnloadCargoDist(byte flags, StationID next, StationID via, StationID source)
{
	if (via == this->station->index) {
		/* this is the final destination, deliver ... */
		if (flags & UL_TRANSFER) {
			/* .. except if explicitly told not to do so ... */
			return UL_TRANSFER;
		} else if (flags & UL_ACCEPTED) {
			return UL_DELIVER;
		} else if (flags & UL_DELIVER) {
			/* .. or if the station suddenly doesn't accept our cargo, but we have an explicit deliver order... */
			return UL_TRANSFER;
		} else {
			/* .. or else if it doesn't accept. */
			return UL_KEEP;
		}
	} else {
		/* packet has to travel on, find out if it can stay on board */
		if (flags & UL_DELIVER) {
			/* order overrides cargodist:
			 * play by the old loading rules here as player is interfering with cargodist
			 * try to deliver, as move has been forced upon us */
			if ((flags & UL_ACCEPTED) && !(flags & UL_TRANSFER) && source != this->station->index) {
				return UL_DELIVER;
			} else {
				/* transfer cargo, as delivering didn't work */
				return UL_TRANSFER;
			}
		} else if (flags & UL_TRANSFER) {
			/* transfer forced */
			return UL_TRANSFER;
		} else if (next == via) {
			/* vehicle goes to the packet's next hop or has nondeterministic order: keep the packet*/
			return UL_KEEP;
		} else {
			/* vehicle goes somewhere else, transfer the packet*/
			return UL_TRANSFER;
		}
	}
}

/**
 * Swap the reserved and packets lists when starting to load cargo. Pull in the
 * "kept" packets which were stored in the reservation list so that we don't
 * have to iterate over them all the time.
 * @pre this->packets.empty()
 */
void VehicleCargoList::SwapReserved()
{
	assert(this->packets.empty());
	this->packets.swap(this->reserved);
	this->reserved_count = 0;
}

/**
 * Moves the given amount of cargo from a vehicle to a station.
 * Depending on the value of flags the side effects of this function differ:
 *  - OUFB_UNLOAD_IF_POSSIBLE and dest->acceptance_pickup & GoodsEntry::ACCEPTANCE:
 *  	packets are accepted here and may be unloaded and/or delivered (=destroyed);
 *  	if not using cargodist: all packets are unloaded and delivered
 *  	if using cargodist: only packets which have this station as final destination are unloaded and delivered.
 *  	if using cargodist: other packets may or may not be unloaded, depending on next_station.
 *  	if GoodsEntry::ACCEPTANCE is not set and using cargodist: packets may still be unloaded, but not delivered.
 *  - OUFB_UNLOAD: unload all packets unconditionally;
 *  	if OUF_UNLOAD_IF_POSSIBLE set and OUFB_TRANSFER not set: also deliver packets (no matter if using cargodist).
 *  - OUFB_TRANSFER: don't deliver any packets;
 *  	overrides delivering aspect of OUFB_UNLOAD_IF_POSSIBLE.
 * @param source       Vehicle cargo list to take the cargo from.
 * @param max_unload   Maximum amount of cargo entities to move.
 * @param flags        How to handle the moving (side effects).
 * @param next         Next unloading station in the vehicle's order list.
 * @param has_stopped  Vehicle has stopped at this station before, so don't update the flow stats for kept cargo.
 * @param payment      Payment object to be updated when delivering/transferring.
 * @return Number of cargo entities actually moved.
 */
uint StationCargoList::TakeFrom(VehicleCargoList *source, uint max_unload, OrderUnloadFlags order_flags, StationID next, bool has_stopped, CargoPayment *payment)
{
	uint remaining_unload = max_unload;
	uint unloaded;
	byte flags = this->GetUnloadFlags(order_flags);
	GoodsEntry *dest = &this->station->goods[this->cargo];
	UnloadType action;

	for (VehicleCargoList::Iterator c = source->packets.begin(); c != source->packets.end() && remaining_unload > 0;) {
		StationID cargo_source = (*c)->source;
		FlowStatSet &flows = dest->flows[cargo_source];
		FlowStatSet::iterator begin = flows.begin();
		StationID via = (begin != flows.end() ? begin->Via() : INVALID_STATION);
		if (via != INVALID_STATION && next != INVALID_STATION) {
			/* use cargodist unloading*/
			action = this->WillUnloadCargoDist(flags, next, via, cargo_source);
		} else {
			/* there is no plan: use normal unloading */
			action = this->WillUnloadOld(flags, cargo_source);
		}

		switch(action) {
			case UL_DELIVER:
				unloaded = source->DeliverPacket(c, remaining_unload, payment);
				if (via != INVALID_STATION) {
					if (via == this->station->index) {
						dest->UpdateFlowStats(flows, begin, unloaded);
					} else {
						dest->UpdateFlowStats(flows, unloaded, this->station->index);
					}
				}
				remaining_unload -= unloaded;
				break;
			case UL_TRANSFER:
				/* TransferPacket may split the packet and return the transferred part */
				if (via == this->station->index) {
					via = (++begin != flows.end()) ? begin->Via() : INVALID_STATION;
				}
				unloaded = source->TransferPacket(c, remaining_unload, this, payment, via);
				if (via != INVALID_STATION) {
					dest->UpdateFlowStats(flows, begin, unloaded);
				}
				remaining_unload -= unloaded;
				break;
			case UL_KEEP:
				unloaded = source->KeepPacket(c);
				if (via != INVALID_STATION && next != INVALID_STATION && !has_stopped) {
					if (via == next) {
						dest->UpdateFlowStats(flows, begin, unloaded);
					} else {
						dest->UpdateFlowStats(flows, unloaded, next);
					}
				}
				break;
			default:
				NOT_REACHED();
		}
	}
	return max_unload - remaining_unload;
}

/**
 * Update the cached values to reflect the removal of this packet.
 * Decreases count, feeder share and days_in_transit.
 * @param cp Packet to be removed from cache.
 */
void VehicleCargoList::RemoveFromCache(const CargoPacket *cp)
{
	this->feeder_share -= cp->feeder_share;
	this->Parent::RemoveFromCache(cp);
}

/**
 * Update the cache to reflect adding of this packet.
 * Increases count, feeder share and days_in_transit.
 * @param cp New packet to be inserted.
 */
void VehicleCargoList::AddToCache(const CargoPacket *cp)
{
	this->feeder_share += cp->feeder_share;
	this->Parent::AddToCache(cp);
}

/**
 * Moves the given amount of cargo to another vehicle (during autoreplace).
 * @param dest         Destination to move the cargo to.
 * @param cap          Maximum amount of cargo entities to move.
 * @return             Amount of cargo actually moved.
 */
uint VehicleCargoList::MoveTo(VehicleCargoList *dest, uint cap)
{
	uint orig_cap = cap;
	Iterator it = packets.begin();
	while (it != packets.end() && cap > 0) {
		cap -= MovePacket(dest, it, cap);
	}
	return orig_cap - cap;
}

/**
 * Ages the all cargo in this list.
 */
void VehicleCargoList::AgeCargo()
{
	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		CargoPacket *cp = *it;
		/* If we're at the maximum, then we can't increase no more. */
		if (cp->days_in_transit == 0xFF) continue;

		cp->days_in_transit++;
		this->cargo_days_in_transit += cp->count;
	}
}

/*
 *
 * Station cargo list implementation
 *
 */

/**
 * build unload flags from order flags and station acceptance.
 * @param order_flags order flags to check for forced transfer/deliver
 * @return some combination of UL_ACCEPTED, UL_DELIVER and UL_TRANSFER
 */
FORCEINLINE byte StationCargoList::GetUnloadFlags(OrderUnloadFlags order_flags)
{
	byte flags = 0;
	if (HasBit(this->station->goods[this->cargo].acceptance_pickup, GoodsEntry::ACCEPTANCE)) {
		flags |= UL_ACCEPTED;
	}
	if (order_flags & OUFB_UNLOAD) {
		flags |= UL_DELIVER;
	}
	if (order_flags & OUFB_TRANSFER) {
		flags |= UL_TRANSFER;
	}
	return flags;
}

/**
 * Appends the given cargo packet to the range of packets with the same next station
 * @warning After appending this packet may not exist anymore!
 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
 * @param next the next hop
 * @param cp the cargo packet to add
 * @pre cp != NULL
 */
void StationCargoList::Append(StationID next, CargoPacket *cp)
{
	assert(cp != NULL);
	this->AddToCache(cp);

	StationCargoPacketMap::List &list = this->packets[next];
	for (StationCargoPacketMap::List::reverse_iterator it(list.rbegin()); it != list.rend(); it++) {
		CargoPacket *icp = *it;
		if (StationCargoList::AreMergable(icp, cp) && icp->count + cp->count <= CargoPacket::MAX_COUNT) {
			icp->Merge(cp);
			return;
		}
	}

	/* The packet could not be merged with another one */
	list.push_back(cp);
}

/**
 * Move packets from a specific range in this list to a vehicle.
 * @param dest Cargo list the packets will be moved to.
 * @param cap Maximum amount of cargo to move.
 * @param begin Begin of the range to take packets from.
 * @param end End of the range to take packets from.
 * @param reserve If the packets should be loaded on or reserved for the vehicle.
 * @return Amount of cargo that has been moved.
 */
uint StationCargoList::MovePackets(VehicleCargoList *dest, uint cap, Iterator begin, Iterator end, bool reserve)
{
	uint orig_cap = cap;
	while (begin != end && cap > 0) {
		cap -= this->MovePacket(dest, begin, cap, this->station->xy, reserve);
	}
	return orig_cap - cap;
}

/**
 * Move suitable packets from this list to a vehicle.
 * @param dest Vehicle cargo list to move packets to.
 * @param cap Maximum amount of cargo to be moved.
 * @param next Next station the vehicle will stop at.
 * @param reserve If the packets should be loaded on or reserved for the vehicle.
 * @return Amount of cargo that has been moved.
 */
uint StationCargoList::MoveTo(VehicleCargoList *dest, uint cap, StationID next, bool reserve)
{
	uint orig_cap = cap;
	if (next != INVALID_STATION) {
		std::pair<Iterator, Iterator> bounds(this->packets.equal_range(next));
		cap -= this->MovePackets(dest, cap, bounds.first, bounds.second, reserve);
		if (cap > 0) {
			bounds = this->packets.equal_range(INVALID_STATION);
			cap -= this->MovePackets(dest, cap, bounds.first, bounds.second, reserve);
		}
	} else {
		cap -= this->MovePackets(dest, cap, this->packets.begin(), this->packets.end(), reserve);
	}
	return orig_cap - cap;
}

/**
 * Route all packets with station "to" as next hop to a different place.
 * @param to station to exclude from routing.
 */
void StationCargoList::RerouteStalePackets(StationID to)
{
	std::pair<Iterator, Iterator> range(this->packets.equal_range(to));
	for (Iterator it(range.first); it != range.second && it.GetKey() == to;) {
		CargoPacket *packet = *it;
		this->packets.erase(it++);
		StationID next = this->station->goods[this->cargo].UpdateFlowStatsTransfer(packet->source, packet->count, this->station->index);
		assert(next != to);

		/* legal, as insert doesn't invalidate iterators in the MultiMap, however
		 * this might insert the packet between range.first and range.second (which might be end())
		 * This is why we check for GetKey above to avoid infinite loops
		 */
		this->packets.Insert(next, packet);
	}
}

/**
 * Truncate where each destination loses roughly the same percentage of its cargo.
 * This is done by randomizing the selection of packets to be removed. Also count
 * the cargo by origin station.
 * @param max_remaining Maximum amount of cargo to keep in the station.
 * @param cargo_per_source Container for counting the cargo by origin list.
 */
void StationCargoList::CountAndTruncate(uint max_remaining, StationCargoAmountMap &cargo_per_source)
{
	uint prev_count = this->count;
	uint loop = 0;
	while (this->count > max_remaining) {
		for (Iterator it(this->packets.begin()); it != this->packets.end();) {
			CargoPacket *packet = *it;
			if (loop == 0) cargo_per_source[packet->source] += packet->count;

			if (RandomRange(prev_count) < max_remaining) {
				++it;
				continue;
			}

			uint diff = this->count - max_remaining;
			if (packet->count > diff) {
				packet->count -= diff;
				this->count = max_remaining;
				this->cargo_days_in_transit -= packet->days_in_transit * diff;
				if (loop > 0) {
					return;
				} else {
					++it;
				}
			} else {
				this->packets.erase(it++);
				this->RemoveFromCache(packet);
				delete packet;
			}
		}
		loop++;
	}
}

/**
 * Invalidates the cached data and rebuilds it.
 */
void VehicleCargoList::InvalidateCache()
{
	this->feeder_share = 0;
	this->reserved_count = 0;
	this->Parent::InvalidateCache();
	for (ConstIterator it(this->reserved.begin()); it != this->reserved.end(); it++) {
		this->AddToCache(*it);
		this->reserved_count += (*it)->count;
	}
}

/**
 * Assign the cargo list to a goods entry.
 * @param station the station the cargo list is assigned to
 * @param cargo the cargo the list is assigned to
 */
void StationCargoList::AssignTo(Station *station, CargoID cargo)
{
	assert(this->station == NULL);
	assert(station != NULL && cargo != INVALID_CARGO);
	this->station = station;
	this->cargo = cargo;
}


/*
 * We have to instantiate everything we want to be usable.
 */
template class CargoList<VehicleCargoList, CargoPacketList>;
template class CargoList<StationCargoList, StationCargoPacketMap>;
