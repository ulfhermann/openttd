/** @file demands.h Definition of demand calculating link graph handler. */

#include "demands.h"
#include "../station_base.h"
#include "../settings_type.h"
#include "../newgrf_cargo.h"
#include "../cargotype.h"
#include "../core/math_func.hpp"
#include <list>
#include <iostream>

typedef std::list<NodeID> NodeList;

void DemandCalculator::PrintDemandMatrix(LinkGraphComponent * graph) {
	for (NodeID from = 0; from < graph->GetSize(); ++from) {
		std::cout << graph->GetNode(from).station << "\t";
		for(NodeID to = 0; to < graph->GetSize(); ++to) {
			if (from == to) {
				std::cout << graph->GetNode(from).supply << "\t";
			} else {
				std::cout << graph->GetEdge(from, to).demand << "\t";
			}
		}
		std::cout << "\n";
	}
}

void DemandCalculator::CalcSymmetric(LinkGraphComponent * graph) {
	NodeList nodes;
	uint supply_sum = 0;
	for(NodeID node = 0; node < graph->GetSize(); node++) {
		Node & n = graph->GetNode(node);
		if (n.demand > 0 && n.supply > 0) {
			nodes.push_back(node);
			supply_sum += n.supply;
		}
	}

	if (supply_sum == 0) {
		return;
	}

	while(!nodes.empty()) {
		NodeID node1 = nodes.front();
		nodes.pop_front();

		Node & from = graph->GetNode(node1);

		for(NodeList::iterator i = nodes.begin(); i != nodes.end();) {
			NodeID node2 = *i;
			Edge & forward = graph->GetEdge(node1, node2);
			Edge & backward = graph->GetEdge(node2, node1);

			Node & to = graph->GetNode(node2);

			uint demand = from.undelivered_supply * to.undelivered_supply * (max_distance - forward.distance) / max_distance / supply_sum + 1;

			forward.demand += demand;
			backward.demand += demand;

			assert(demand <= from.undelivered_supply);
			assert(demand <= to.undelivered_supply);

			from.undelivered_supply -= demand;
			to.undelivered_supply -= demand;

			if (to.undelivered_supply == 0) {
				nodes.erase(i++);
			} else {
				++i;
			}
			if (from.undelivered_supply == 0) {
				break;
			}
		}
		if (from.undelivered_supply != 0) {
			if (nodes.empty()) {
				// only one node left
				return;
			} else {
				nodes.push_back(node1);
			}
		}
	}
}


void DemandCalculator::CalcAntiSymmetric(LinkGraphComponent * graph) {
	NodeList supplies;
	NodeList demands;
	uint supply_sum = 0;
	uint num_demands = 0;
	for(NodeID node = 0; node < graph->GetSize(); node++) {
		Node & n = graph->GetNode(node);
		if (n.supply > 0) {
			supplies.push_back(node);
			supply_sum += n.supply;
		}
		if (n.demand > 0) {
			demands.push_back(node);
			num_demands++;
		}
	}

	if (supply_sum == 0 || num_demands == 0) {
		return;
	}

	uint demand_per_node = max(supply_sum / num_demands, (uint)1);

	while(!supplies.empty()) {
		NodeID node1 = supplies.front();
		supplies.pop_front();

		Node & from = graph->GetNode(node1);

		for(uint i = 0; i < num_demands; ++i) {
			NodeID node2 = demands.front();
			demands.pop_front();
			demands.push_back(node2);
			if (node1 == node2) {
				continue;
			}
			Edge & edge = graph->GetEdge(node1, node2);
			uint demand = from.undelivered_supply * demand_per_node * (max_distance - edge.distance) / max_distance / supply_sum + 1;
			edge.demand += demand;
			assert(demand <= from.undelivered_supply);
			from.undelivered_supply -= demand;
			if (from.undelivered_supply == 0) {
				break;
			}
		}
		if (from.undelivered_supply != 0) {
			supplies.push_back(node1);
		}
	}
}

void DemandCalculator::Run(LinkGraphComponent * graph) {
	CargoID cargo = graph->GetCargo();
	const LinkGraphSettings & settings = graph->GetSettings();
	DistributionType type = settings.demand_default;
	if (IsCargoInClass(cargo, CC_PASSENGERS)) {
		type = settings.demand_pax;
	} else if (IsCargoInClass(cargo, CC_MAIL)) {
		type = settings.demand_mail;
	} else if (IsCargoInClass(cargo, CC_EXPRESS)) {
		type = settings.demand_express;
	} else if (IsCargoInClass(cargo, CC_ARMOURED)) {
		type = settings.demand_armoured;
	}

	switch (type) {
	case DT_SYMMETRIC:
		CalcSymmetric(graph);
		break;
	case DT_ANTISYMMETRIC:
		CalcAntiSymmetric(graph);
		break;
	default:
		/* ignore */
		break;
	}
}
