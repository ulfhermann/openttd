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
				std::cout << graph->GetEdge(from, to).distance << ":" << graph->GetEdge(from, to).demand << "\t";
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
	uint num_supplies = 0;
	for(NodeID node = 0; node < graph->GetSize(); node++) {
		Node & n = graph->GetNode(node);
		if (n.supply > 0) {
			supplies.push_back(node);
			supply_sum += n.supply;
			num_supplies++;
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
	uint chance = 0;

	while(!supplies.empty() && !demands.empty()) {
		NodeID node1 = supplies.front();
		supplies.pop_front();

		Node & from = graph->GetNode(node1);

		for(uint i = 0; i < num_demands; ++i) {
			assert(!demands.empty());
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
			}
			Node & to = graph->GetNode(node2);
			Edge & forward = graph->GetEdge(node1, node2);
			Edge & backward = graph->GetEdge(node2, node1);

			int32 supply = from.supply;
			if (this->mod_size > 0) {
				supply = max(1, (int32)(supply * to.supply * this->mod_size / 100 / demand_per_node));
			}
			assert(supply > 0);

			/* scale the distance by mod_dist around max_distance */
			int32 distance = this->max_distance - (this->max_distance - (int32)forward.distance) * this->mod_dist / 100;

			/* scale the accuracy by distance around accuracy / 2 */
			int32 divisor = this->accuracy * (this->mod_dist - 50) / 100 + this->accuracy * distance / this->max_distance + 1;
			assert(divisor > 0);

			uint demand_forw = 0;
			if (divisor < supply) {
				demand_forw = supply / divisor;
			} else if (++chance > this->accuracy * num_demands * num_supplies) {
				/* after some trying distribute demand also to other nodes */
				demand_forw = 1;
			}

			demand_forw = min(demand_forw, from.undelivered_supply);

			if (this->mod_size > 0 && from.demand > 0) {
				uint demand_back = demand_forw * this->mod_size / 100;
				if (demand_back > to.undelivered_supply) {
					demand_back = to.undelivered_supply;
					demand_forw = demand_back * 100 / this->mod_size;
				}
				backward.demand += demand_back;
				backward.unsatisfied_demand += demand_back;
				to.undelivered_supply -= demand_back;
			}

			forward.demand += demand_forw;
			forward.unsatisfied_demand += demand_forw;
			from.undelivered_supply -= demand_forw;

			if (this->mod_size == 0 || to.undelivered_supply > 0) {
				demands.push_back(node2);
			} else {
				num_demands--;
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
	if (this->mod_dist > 100) {
		/* increase effect of mod_dist > 100 */
		int over100 = this->mod_dist - 100;
		this->mod_dist = 100 + over100 * over100;
	}

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
