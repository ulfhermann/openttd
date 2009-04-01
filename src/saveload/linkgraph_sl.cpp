/*
 * cargodist_sl.cpp
 *
 *  Created on: 13.03.2009
 *      Author: alve
 */
#include "../linkgraph.h"
#include "saveload.h"

static uint _num_components;

enum {
	LGRP_GRAPH = 0,
	LGRP_COMPONENT = 1,
	LGRP_NODE = 2,
	LGRP_EDGE = 3,
};


const SaveLoad * GetLinkGraphDesc(uint type) {

	static const SaveLoad _linkgraph_desc[] = {
		SLEG_CONDVAR(           _num_components,  SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, current_colour,   SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, current_station,  SLE_UINT16, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, cargo,            SLE_UINT8,  LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	static const SaveLoad _component_desc[] = {
		 SLE_CONDVAR(Component, num_nodes,        SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Component, component_colour, SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	// edges and nodes are saved in the correct order, so we don't need to save their ids.

	static const SaveLoad _node_desc[] = {
		 SLE_CONDVAR(Node, supply,   SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Node, demand,   SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Node, station,  SLE_UINT16, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	static const SaveLoad _edge_desc[] = {
		 SLE_CONDVAR(Edge, distance, SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Edge, capacity, SLE_UINT,   LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	static const SaveLoad *_lgrp_descs[] = {
		_linkgraph_desc,
		_component_desc,
		_node_desc,
		_edge_desc,
	};

	return _lgrp_descs[type];

}


static void SaveLoad_Component(Component * comp) {
	for (uint from = 0; from < comp->GetSize(); ++from) {
		SlObject(&comp->GetNode(from), GetLinkGraphDesc(LGRP_NODE));
		for (uint to = 0; to < from; ++to) {
			SlObject(&comp->GetEdge(from, to), GetLinkGraphDesc(LGRP_EDGE));
			SlObject(&comp->GetEdge(to, from), GetLinkGraphDesc(LGRP_EDGE));
		}
	}
}

static void DoSave_LGRP(void *)
{
	for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
		LinkGraph & graph = _link_graphs[cargo];
		_num_components = graph.GetNumComponents();
		SlObject(&graph, GetLinkGraphDesc(LGRP_GRAPH));
		ComponentList & comps = graph.GetComponents();
		for (ComponentList::iterator i = comps.begin(); i != comps.end(); ++i) {
			Component * comp = *i;
			SlObject(comp, GetLinkGraphDesc(LGRP_COMPONENT));
			SaveLoad_Component(comp);
		}
	}
}



static void Load_LGRP()
{
	for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
		LinkGraph & graph = _link_graphs[cargo];
		SlObject(&graph, GetLinkGraphDesc(LGRP_GRAPH));
		for (uint i = 0; i < _num_components; ++i) {
			Component * comp = new Component();
			SlObject(comp, GetLinkGraphDesc(LGRP_COMPONENT));
			comp->SetSize(comp->GetSize());
			SaveLoad_Component(comp);
			graph.AddComponent(comp);
		}
	}
}


static void Save_LGRP() {
	SlAutolength((AutolengthProc*)DoSave_LGRP, NULL);
}

extern const ChunkHandler _linkgraph_chunk_handlers[] = {
	{ 'LGRP', Save_LGRP,      Load_LGRP,      CH_LAST},
};
