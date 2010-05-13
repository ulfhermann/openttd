/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group_type.h Types of a group. */

#ifndef GROUP_TYPE_H
#define GROUP_TYPE_H

typedef uint16 GroupID;

static const GroupID ALL_GROUP     = 0xFFFD;
static const GroupID DEFAULT_GROUP = 0xFFFE; ///< ungrouped vehicles are in this group.
static const GroupID INVALID_GROUP = 0xFFFF;

static const uint MAX_LENGTH_GROUP_NAME_BYTES  =  31; ///< The maximum length of a group name in bytes including '\0'
static const uint MAX_LENGTH_GROUP_NAME_PIXELS = 150; ///< The maximum length of a group name in pixels

struct Group;

#endif /* GROUP_TYPE_H */
