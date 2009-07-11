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

void DemandCalculator::CalcDemand(LinkGraphComponent * graph) {
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
			if (node1 == node2) {
				if (demands.empty() && supplies.empty()) {
					/* only one node with supply and demand left */
					return;
				} else {
					demands.push_back(node2);
					continue;
				}
			} else {
				demands.push_back(node2);
			}
			Node & to = graph->GetNode(node2);
			Edge & forward = graph->GetEdge(node1, node2);
			Edge & backward = graph->GetEdge(node2, node1);

			uint supply = from.supply;
			assert(supply > 0);
			if (this->mod_size > 0) {
				supply = supply * to.supply * this->mod_size / 100 / demand_per_node;
			}

			uint distance = 1;
			if (this->mod_dist > 0) {
				distance = this->max_distance * 100 / (this->max_distance - forward.distance) / this->mod_dist;
			}
			assert(distance > 0);

			uint demand_forw = supply / distance / this->accuracy;

			demand_forw = max(demand_forw, (uint)1);
			demand_forw = min(demand_forw, from.undelivered_supply);

			forward.demand += demand_forw;
			from.undelivered_supply -= demand_forw;

			if (from.demand > 0) {
				uint demand_back = demand_forw * this->mod_size / 100;
				demand_back = min(demand_back, to.undelivered_supply);
				backward.demand += demand_back;
				to.undelivered_supply -= demand_back;
			}


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

	this->accuracy = settings.accuracy;
	this->mod_size = settings.demand_size;
	this->mod_dist = settings.demand_distance;

	switch (type) {
	case DT_SYMMETRIC:
		CalcDemand(graph);
		break;
	case DT_ANTISYMMETRIC:
		this->mod_size = 0;
		CalcDemand(graph);
		break;
	default:
		/* ignore */
		break;
	}
}
