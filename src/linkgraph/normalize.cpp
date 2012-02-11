/** @file normalize.h Definition of normalizing link graph handler. */

#include "../stdafx.h"
#include "normalize.h"
#include <list>

/**
 * Reroute flow from a link between a base or export node and some other node
 * to the corresponding link between a passby node and the other node.
 * @param graph Component the nodes belong to.
 * @param node_id ID of passby node.
 * @param export_id ID of base or export node.
 * @param other_id ID of remote node.
 */
void Normalizer::ReroutePassby(LinkGraphComponent *graph, NodeID node_id, NodeID export_id, NodeID other_id)
{
	Node &node = graph->GetNode(node_id);
	Edge &passby_edge = graph->GetEdge(export_id, other_id);
	assert(node.base_node != other_id);
	assert(graph->GetNode(export_id).passby_flag != IS_PASSBY_NODE);
	if (passby_edge.capacity == 0) return;
	/* next node in passby chain */
	uint reroute = min(node.passby_capacity, passby_edge.capacity);
	node.passby_capacity -= reroute;
	graph->AddEdge(node_id, other_id, reroute);
	passby_edge.capacity -= reroute;
}

Normalizer::Normalizer(LinkGraphComponent *graph)
{
	typedef std::list<std::pair<NodeID, NodeID> > PassbyList;
	PassbyList passby_ends;
	for (NodeID node_id = 0; node_id < graph->GetSize(); ++node_id) {
		Node &node = graph->GetNode(node_id);
		if (node.passby_flag == IS_PASSBY_NODE) {
			/* Passby node:
			 * 1. Make an additional edge of capacity
			 * passby_capacity from passby_node to node for Station
			 * "station".
			 * 2. Reduce capacity of edge from either base
			 * node or export node to "station" by passby_capacity.
			 * 3. Make an additional edge from either export
			 * or base node to passby node.
			 * 4. Set supply, demand to 0.
			 */
			Node &base_node = graph->GetNode(node.base_node);
			NodeID export_id = base_node.export_node == INVALID_NODE ?
					node.base_node : base_node.export_node;
			for (NodeID other_id = 0; other_id != graph->GetSize(); ++other_id) {
				Node &other = graph->GetNode(other_id);
				// TODO: If the passby chain branches we might get some random behaviour
				if (other.passby_flag != IS_PASSBY_NODE && other.station == node.passby_to) {
					/* final end of passby chain */
					passby_ends.push_back(std::make_pair(node_id, other.import_node == INVALID_NODE ?
							other_id : other.import_node));
				} else if (node.passby_capacity > 0 && other.passby_flag == IS_PASSBY_NODE &&
						other.passby_to == node.passby_to) {
					this->ReroutePassby(graph, node_id, export_id, other_id);
				}
			}
			assert(passby_ends.back().first == node_id && passby_ends.back().second < graph->GetSize());
			if (node.passby_capacity > 0) {
				this->ReroutePassby(graph, node_id, export_id, passby_ends.back().second);
			}
			node.supply = 0;
			node.undelivered_supply = 0;
			node.demand = 0;
			graph->AddEdge(export_id, node_id, UINT_MAX);
		} else {
			if (node.import_node != INVALID_NODE && node.import_node != node_id) {
				/* Regular import node:
				 * 1. Make an additional edge from base node to
				 * import node.
				 * 2. Move supply and demand to import node.
				 * 3. Update export_node field of import_node.
				 */
				graph->AddEdge(node_id, node.import_node, UINT_MAX);
				Node &import_node = graph->GetNode(node.import_node);
				import_node.supply = node.supply;
				import_node.undelivered_supply = node.undelivered_supply;
				import_node.demand = node.demand;
				node.supply = 0;
				node.undelivered_supply = 0;
				node.demand = 0;
				import_node.export_node = node.export_node;
				import_node.import_node = node.import_node;
			}

			if (node.export_node != INVALID_NODE && node.export_node != node_id) {
				/* Regular export node:
				 * 1. Move all outgoing links from base node to export
				 * node.
				 * 2. Make an additional edge from base node to export
				 * node.
				 * 3. Clear demand and supply of export node.
				 * 4. Update import_node field of export_node.
				 */
				for (NodeID other_id = 0; other_id != graph->GetSize(); ++other_id) {
					Edge &edge = graph->GetEdge(node_id, other_id);
					if (edge.capacity > 0) {
						graph->GetEdge(node.export_node, other_id).capacity = edge.capacity;
						edge.capacity = 0;
					}
				}
				graph->AddEdge(node_id, node.export_node, UINT_MAX);
				Node &export_node = graph->GetNode(node.export_node);
				export_node.demand = 0;
				export_node.supply = 0;
				export_node.undelivered_supply = 0;
				export_node.import_node = node.import_node;
				export_node.export_node = node.export_node;
			}
		}
	}

	/* Set the passby_to fields to the node IDs. Before they were station IDs. */
	for (PassbyList::iterator i(passby_ends.begin()); i != passby_ends.end(); ++i) {
		graph->GetNode(i->first).passby_to = i->second;
	}
}
