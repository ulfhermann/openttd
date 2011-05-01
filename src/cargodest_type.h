/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_type.h Type definitions for cargo destinations. */

#ifndef CARGODEST_TYPE_H
#define CARGODEST_TYPE_H

/** Flags specifying which cargos are routed to specific destinations. */
enum CargoRoutingMode {
	CRM_TOWN_CARGOS     = 0, ///< Passengers and mail have destinations.
	CRM_INDUSTRY_CARGOS = 1, ///< Cargos produced by industries have destinations.
};

/** Enum specifying if cargo should have fixed or variable destinations. */
enum CargoDistributionMode {
	CDM_FIXED     = 0, ///< Destinations are predetermined without considering the network.
	CDM_REACHABLE = 1, ///< Only reachable destinations are chosen.
};

/** Flags specifying how cargo should be routed. */
enum RoutingFlags {
	RF_WANT_FAST,    ///< Cargo wants to travel as fast as possible.
	RF_WANT_CHEAP,   ///< Cargo wants to travel as cheap as possible.
};

/** Weight modifiers for links. */
enum LinkWeightModifier {
	LWM_ANYWHERE = 1,           ///< Weight modifier for undetermined destinations.
	LWM_TOWN_ANY = 2,           ///< Default modifier for town destinations.
	LWM_INDUSTRY_ANY = 3,       ///< Default modifier for industry destinations.
	LWM_TOWN_BIG = 3,           ///< Weight modifier for big towns.
	LWM_CITY = 4,               ///< Weight modifier for cities.
	LWM_TOWN_NEARBY = 5,        ///< Weight modifier for nearby towns.
	LWM_INDUSTRY_NEARBY = 5,    ///< Weight modifier for nearby industries.
	LWM_INDUSTRY_PRODUCING = 7, ///< Weight modifier for producing industries.
	LWM_INTOWN = 8,             ///< Weight modifier for in-town links.
	LWM_INVALID = 0xff
};

/** Unique identifier for a routing link. */
typedef uint32 RouteLinkID;
struct RouteLink;

#endif /* CARGODEST_TYPE_H */
