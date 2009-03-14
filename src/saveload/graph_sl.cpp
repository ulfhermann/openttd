/*
 * cargodist_sl.cpp
 *
 *  Created on: 13.03.2009
 *      Author: alve
 */
#include "../graph.h"
#include "saveload.h"



static uint _num_nodes;
static uint _num_components;

// you can't forward define enums, so I have to hack around that here.
#define	CDIST_HANDLER 0
#define CDIST_COMPONENT 1
#define CDIST_NODE 2
#define CDIST_EDGE 3


const SaveLoad * getCargoDistDesc(uint type) {

static const SaveLoad _componenthandler_desc[] = {
		SLE_CONDVAR(ComponentHandler, c, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLE_CONDVAR(ComponentHandler, currentStation, SLE_UINT16, CARGODIST_SV, SL_MAX_VERSION),
		SLE_CONDVAR(ComponentHandler, cargo, SLE_UINT8, CARGODIST_SV, SL_MAX_VERSION),
		SLEG_CONDVAR(_num_components, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLE_END()
};

static const SaveLoad _component_desc[] = {
		SLE_CONDVAR(CargoDist, joinTime, SLE_UINT16, CARGODIST_SV, SL_MAX_VERSION),
		SLE_CONDVAR(CargoDist, componentColour, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLEG_CONDVAR(_num_nodes, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLE_END()
};

// edges and nodes are saved in the correct order, so we don't need to save their ids.

static const SaveLoad _node_desc[] = {
		SLE_CONDVAR(Node, supply, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLE_CONDVAR(Node, station, SLE_UINT16, CARGODIST_SV, SL_MAX_VERSION),
		SLE_END()
};

static const SaveLoad _edge_desc[] = {
		SLE_CONDVAR(Edge, distance, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLE_CONDVAR(Edge, capacity, SLE_UINT, CARGODIST_SV, SL_MAX_VERSION),
		SLE_END()
};

	static const SaveLoad *_cdist_descs[] = {
		_componenthandler_desc,
		_component_desc,
		_node_desc,
		_edge_desc,
	};

	return _cdist_descs[type];

}


static void saveLoadGraph(CargoDistGraph & graph) {
	for (uint from = 0; from < _num_nodes; ++from) {
		SlObject(&graph.node(from), getCargoDistDesc(CDIST_NODE));
		for (uint to = 0; to < from; ++to) {
			SlObject(&graph.edge(from, to), getCargoDistDesc(CDIST_EDGE));
			SlObject(&graph.edge(to, from), getCargoDistDesc(CDIST_EDGE));
		}
	}
}

static void DoSave_CDIS(void *)
{
	for(CargoID cargo = 0; cargo < NUM_CARGO; ++cargo) {
		ComponentHandler & handler = CargoDist::handlers[cargo];
		_num_components = handler.getNumComponents();
		SlObject(&handler, getCargoDistDesc(CDIST_HANDLER));
		CargoDistList & cdists = handler.getComponents();
		for (CargoDistList::const_iterator i = cdists.begin(); i != cdists.end(); ++i) {
			CargoDist * cdist = *i;
			CargoDistGraph & graph = cdist->getGraph();
			_num_nodes = graph.size();
			SlObject(cdist, getCargoDistDesc(CDIST_COMPONENT));
			saveLoadGraph(graph);
		}
	}
}



static void Load_CDIS()
{
	for(CargoID cargo = 0; cargo < NUM_CARGO; ++cargo) {
		ComponentHandler & handler = CargoDist::handlers[cargo];
		SlObject(&handler, getCargoDistDesc(CDIST_HANDLER));
		for (uint i = 0; i < _num_components; ++i) {
			CargoDist * cdist = new CargoDist(cargo);
			SlObject(cdist, getCargoDistDesc(CDIST_COMPONENT));
			CargoDistGraph & graph = cdist->getGraph();
			graph.setSize(_num_nodes);
			saveLoadGraph(graph);
			handler.addComponent(cdist);
		}
	}
}


static void Save_CDIS() {
	SlAutolength((AutolengthProc*)DoSave_CDIS, NULL);
}

extern const ChunkHandler _cargodist_chunk_handlers[] = {
	{ 'CDIS', Save_CDIS,      Load_CDIS,      CH_LAST},
};
