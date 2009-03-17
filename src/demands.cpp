/*
 * demands.cpp
 *
 *  Created on: 08.03.2009
 *      Author: alve
 */

#include "demands.h"
#include "station_base.h"
#include "settings_type.h"
#include "newgrf_cargo.h"
#include "cargotype.h"
#include <list>
#include <iostream>

typedef std::list<uint> NodeList;

uint DemandCalculator::_max_distance;
DistributionType DemandCalculator::_distribution_types[NUM_CARGO];

void DemandCalculator::PrintDemandMatrix(Component * graph) {
	for (uint from = 0; from < graph->GetSize(); ++from) {
		std::cout << graph->GetNode(from).station << "\t";
		for(uint to = 0; to < graph->GetSize(); ++to) {
			if (from == to) {
				std::cout << graph->GetNode(from).supply << "\t";
			} else {
				std::cout << graph->GetEdge(from, to).demand << "\t";
			}
		}
		std::cout << "\n";
	}
}

void DemandCalculator::CalcSymmetric(Component * graph) {
	NodeList nodes;
	uint supply_sum = 0;
	for(uint node = 0; node < graph->GetSize(); node++) {
		nodes.push_back(node);
		supply_sum += graph->GetNode(node).supply;
	}

	if (supply_sum == 0) {
		return;
	}

	while(!nodes.empty()) {
		uint node1 = nodes.front();
		nodes.pop_front();

		Node & from = graph->GetNode(node1);

		for(NodeList::iterator i = nodes.begin(); i != nodes.end(); ++i) {
			uint node2 = *i;
			Edge & forward = graph->GetEdge(node1, node2);
			Edge & backward = graph->GetEdge(node2, node1);

			Node & to = graph->GetNode(node2);

			uint demand = from.supply * to.supply * (_max_distance - forward.distance) / _max_distance / supply_sum + 1;
			demand = min(demand, from.supply);
			demand = min(demand, to.supply);

			forward.demand += demand;
			backward.demand += demand;

			from.supply -= demand;
			to.supply -= demand;

			if (to.supply == 0) {
				i = nodes.erase(i);
				if (nodes.empty()) {
					// only one node left
					return;
				} else {
					--i;
				}
			}
			if (from.supply == 0) {
				break;
			}
		}
		if (from.supply != 0) {
			if (nodes.empty()) {
				// only one node left
				return;
			} else {
				nodes.push_back(node1);
			}
		}
	}
}


void DemandCalculator::CalcAntiSymmetric(Component * graph) {
	NodeList nodes;
	uint supply_sum = 0;
	for(uint node = 0; node < graph->GetSize(); node++) {
		nodes.push_back(node);
		supply_sum += graph->GetNode(node).supply;
	}

	if (supply_sum == 0) {
		return;
	}

	while(!nodes.empty()) {
		uint node1 = nodes.front();
		nodes.pop_front();

		Node & from = graph->GetNode(node1);

		for(NodeList::iterator i = nodes.begin(); i != nodes.end(); ++i) {
			uint node2 = *i;
			Edge & forward = graph->GetEdge(node1, node2);
			Edge & backward = graph->GetEdge(node2, node1);

			Node & to = graph->GetNode(node2);

			uint demand = from.supply * to.supply * (_max_distance - forward.distance) / _max_distance / supply_sum + 1;
			demand = min(demand, from.supply);
			demand = min(demand, to.supply);

			forward.demand += demand;
			backward.demand += demand;

			from.supply -= demand;
			to.supply -= demand;

			if (to.supply == 0) {
				i = nodes.erase(i);
				if (nodes.empty()) {
					// only one node left
					return;
				} else {
					--i;
				}
			}
			if (from.supply == 0) {
				break;
			}
		}
		if (from.supply != 0) {
			if (nodes.empty()) {
				// only one node left
				return;
			} else {
				nodes.push_back(node1);
			}
		}
	}
}

void DemandCalculator::Run(Component * graph) {
	switch (_distribution_types[cargo]) {
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

	PrintDemandMatrix(graph);
}

void InitializeDemands() {
	DemandCalculator::_max_distance = MapMaxX() + MapMaxY();
	EconomySettings & settings = _settings_game.economy;
	DistributionType * types = DemandCalculator::_distribution_types;
	for (CargoID c = 0; c < NUM_CARGO; ++c) {
		if (IsCargoInClass(c, CC_PASSENGERS)) {
			types[c] = settings.demand_pax;
		} else if (IsCargoInClass(c, CC_MAIL)) {
			types[c] = settings.demand_mail;
		} else if (IsCargoInClass(c, CC_EXPRESS)) {
			types[c] = settings.demand_express;
		} else if (IsCargoInClass(c, CC_ARMOURED)) {
			types[c] = settings.demand_armoured;
		} else {
			types[c] = settings.demand_default;
		}
	}
}
