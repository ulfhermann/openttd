/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_commons.h This file simplyfies and embeds a common mechanism of
 * loading/saving and mapping of grf entities.
 */

#ifndef NEWGRF_COMMONS_H
#define NEWGRF_COMMONS_H

#include "tile_cmd.h"

/**
 * Maps an entity id stored on the map to a GRF file.
 * Entities are objects used ingame (houses, industries, industry tiles) for
 * which we need to correlate the ids from the grf files with the ones in the
 * the savegames themselves.
 * An array of EntityIDMapping structs is saved with the savegame so
 * that those GRFs can be loaded in a different order, or removed safely. The
 * index in the array is the entity's ID stored on the map.
 *
 * The substitute ID is the ID of an original entity that should be used instead
 * if the GRF containing the new entity is not available.
 */
struct EntityIDMapping {
	uint32 grfid;          ///< The GRF ID of the file the entity belongs to
	uint8  entity_id;      ///< The entity ID within the GRF file
	uint8  substitute_id;  ///< The (original) entity ID to use if this GRF is not available
};

class OverrideManagerBase {
protected:
	uint16 *entity_overrides;
	uint32 *grfid_overrides;

	uint16 max_offset;       ///< what is the length of the original entity's array of specs
	uint16 max_new_entities; ///< what is the amount of entities, old and new summed

	uint16 invalid_ID;       ///< ID used to dected invalid entities;
	virtual bool CheckValidNewID(uint16 testid) { return true; }

public:
	EntityIDMapping *mapping_ID; ///< mapping of ids from grf files.  Public out of convenience

	OverrideManagerBase(uint16 offset, uint16 maximum, uint16 invalid);
	virtual ~OverrideManagerBase();

	void ResetOverride();
	void ResetMapping();

	void Add(uint8 local_id, uint32 grfid, uint entity_type);
	virtual uint16 AddEntityID(byte grf_local_id, uint32 grfid, byte substitute_id);

	uint16 GetSubstituteID(uint16 entity_id);
	virtual uint16 GetID(uint8 grf_local_id, uint32 grfid);

	inline uint16 GetMaxMapping() { return max_new_entities; }
	inline uint16 GetMaxOffset() { return max_offset; }
};


struct HouseSpec;
class HouseOverrideManager : public OverrideManagerBase {
public:
	HouseOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}
	void SetEntitySpec(const HouseSpec *hs);
};


struct IndustrySpec;
class IndustryOverrideManager : public OverrideManagerBase {
public:
	IndustryOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	virtual uint16 AddEntityID(byte grf_local_id, uint32 grfid, byte substitute_id);
	virtual uint16 GetID(uint8 grf_local_id, uint32 grfid);
	void SetEntitySpec(IndustrySpec *inds);
};


struct IndustryTileSpec;
class IndustryTileOverrideManager : public OverrideManagerBase {
protected:
	virtual bool CheckValidNewID(uint16 testid) { return testid != 0xFF; }
public:
	IndustryTileOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(const IndustryTileSpec *indts);
};

struct AirportSpec;
class AirportOverrideManager : public OverrideManagerBase {
public:
	AirportOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(AirportSpec *inds);
};

struct AirportTileSpec;
class AirportTileOverrideManager : public OverrideManagerBase {
protected:
	virtual bool CheckValidNewID(uint16 testid) { return testid != 0xFF; }
public:
	AirportTileOverrideManager(uint16 offset, uint16 maximum, uint16 invalid) :
			OverrideManagerBase(offset, maximum, invalid) {}

	void SetEntitySpec(const AirportTileSpec *ats);
};

extern HouseOverrideManager _house_mngr;
extern IndustryOverrideManager _industry_mngr;
extern IndustryTileOverrideManager _industile_mngr;
extern AirportOverrideManager _airport_mngr;
extern AirportTileOverrideManager _airporttile_mngr;

uint32 GetTerrainType(TileIndex tile);
TileIndex GetNearbyTile(byte parameter, TileIndex tile);
uint32 GetNearbyTileInformation(TileIndex tile);

/** Data related to the handling of grf files. */
struct GRFFileProps {
	uint16 subst_id;
	uint16 local_id;                      ///< id defined by the grf file for this entity
	struct SpriteGroup *spritegroup;      ///< pointer to the different sprites of the entity
	const struct GRFFile *grffile;        ///< grf file that introduced this entity
	uint16 override;                      ///< id of the entity been replaced by
};

#endif /* NEWGRF_COMMONS_H */
