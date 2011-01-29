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

/** Flags specifying the demand mode for cargo. */
enum CargoRoutingMode {
	CRM_OFF,             ///< No routing, original behaviour
	CRM_FIXED_DEST,      ///< Fixed destinations
};

/** Flags specifying how cargo should be routed. */
enum RoutingFlags {
	RF_WANT_FAST,    ///< Cargo wants to travel as fast as possible.
	RF_WANT_CHEAP,   ///< Cargo wants to travel as cheap as possible.
};

/** Unique identifier for a routing link. */
typedef uint32 RouteLinkID;
struct RouteLink;

#endif /* CARGODEST_TYPE_H */
