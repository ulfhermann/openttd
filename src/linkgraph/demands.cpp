/** @file demands.h Definition of demand calculating link graph handler. */

#include "../stdafx.h"
#include "../station_base.h"
#include "../settings_type.h"
#include "../newgrf_cargo.h"
#include "../cargotype.h"
#include "../core/math_func.hpp"
#include "demands.h"
#include <list>

typedef std::list<NodeID> NodeList;

/**
 * Set the demands between two nodes using the given base demand. In symmetric mode
 * this sets demands in both directions.
 * @param graph The link graph.
 * @param from_id The supplying node.
 * @þaram to_id The receiving node.
 * @param demand_forw Demand calculated for the "forward" direction.
 */
void SymmetricScaler::SetDemands(LinkGraphComponent *graph, NodeID from_id, NodeID to_id, uint demand_forw)
{
	if (graph->GetNode(from_id).demand > 0) {
		uint demand_back = demand_forw * this->mod_size / 100;
		uint undelivered = graph->GetNode(to_id).undelivered_supply;
		if (demand_back > undelivered) {
			demand_back = undelivered;
			demand_forw = max(1U, demand_back * 100 / this->mod_size);
		}
		this->Scaler::SetDemands(graph, to_id, from_id, demand_back);
	}

	this->Scaler::SetDemands(graph, from_id, to_id, demand_forw);
}

/**
 * Set the demands between two nodes using the given base demand. In asymmetric mode
 * this only sets demand in the "forward" direction.
 * @param graph The link graph.
 * @param from_id The supplying node.
 * @þaram to_id The receiving node.
 * @param demand_forw Demand calculated for the "forward" direction.
 */
inline void Scaler::SetDemands(LinkGraphComponent *graph, NodeID from_id, NodeID to_id, uint demand_forw)
{
	NodeID export_id = from_id;
	NodeID import_id = to_id;
	Node *from_node = &graph->GetNode(from_id);
	Node *to_node = &graph->GetNode(to_id);

	if (from_node->export_node != INVALID_NODE) {
		export_id = from_node->export_node;
		if (from_node->import_node == from_node->export_node) {
			/* passby */
			from_node = &graph->GetNode(from_node->export_node);
			if (from_node->export_node != INVALID_NODE) {
				export_id = from_node->export_node;
			}
		}
	}
	if (to_node->import_node != INVALID_NODE) {
		import_id = to_node->import_node;
		if (to_node->import_node == to_node->export_node) {
			/* passby */
			to_node = &graph->GetNode(to_node->import_node);
			if (to_node->import_node != INVALID_NODE) {
				import_id = to_node->import_node;
			}
		}
	}

	Edge &forward = graph->GetEdge(export_id, import_id);
	forward.demand += demand_forw;
	forward.unsatisfied_demand += demand_forw;
	graph->GetNode(from_id).undelivered_supply -= demand_forw;
}

/**
 * Do the actual demand calculation, called from constructor.
 * @param graph Component to calculate the demands for.
 */
template<class Tscaler>
void DemandCalculator::CalcDemand(LinkGraphComponent *graph, Tscaler scaler)
{
	NodeList supplies;
	NodeList demands;
	uint num_supplies = 0;
	uint num_demands = 0;

	for (NodeID node = 0; node < graph->GetSize(); node++) {
		Node &n = graph->GetNode(node);
		scaler.AddNode(n);
		if (n.supply > 0) {
			supplies.push_back(node);
			num_supplies++;
		}
		if (n.demand > 0) {
			demands.push_back(node);
			num_demands++;
		}
	}

	if (num_supplies == 0 || num_demands == 0) return;

	/* mean acceptance attributed to each node. If the distribution is
	 * symmetric this is relative to remote supply, otherwise it is
	 * relative to remote demand.
	 */
	scaler.SetDemandPerNode(num_demands);
	uint chance = 0;

	while (!supplies.empty() && !demands.empty()) {
		NodeID node1 = supplies.front();
		supplies.pop_front();

		Node &from = graph->GetNode(node1);

		for (uint i = 0; i < num_demands; ++i) {
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
			Node &to = graph->GetNode(node2);

			int32 supply = scaler.EffectiveSupply(from, to);
			assert(supply > 0);

			/* scale the distance by mod_dist around max_distance */
			int32 distance = this->max_distance - (this->max_distance -
					(int32)graph->GetEdge(node1, node2).distance) * this->mod_dist / 100;

			/* scale the accuracy by distance around accuracy / 2 */
			int32 divisor = this->accuracy * (this->mod_dist - 50) / 100 +
					this->accuracy * distance / this->max_distance + 1;

			assert(divisor > 0);

			uint demand_forw = 0;
			if (divisor <= supply) {
				/* at first only distribute demand if
				 * effective supply / accuracy divisor >= 1
				 * Others are too small or too far away to be considered.
				 */
				demand_forw = supply / divisor;
			} else if (++chance > this->accuracy * num_demands * num_supplies) {
				/* After some trying, if there is still supply left, distribute
				 * demand also to other nodes.
				 */
				demand_forw = 1;
			}

			demand_forw = min(demand_forw, from.undelivered_supply);

			scaler.SetDemands(graph, node1, node2, demand_forw);

			if (scaler.DemandLeft(to)) {
				demands.push_back(node2);
			} else {
				num_demands--;
			}

			if (from.undelivered_supply == 0) break;

		}
		if (from.undelivered_supply != 0) {
			supplies.push_back(node1);
		} else {
			num_supplies--;
		}
	}
}

/**
 * Create the DemandCalculator and immediately do the calculation.
 * @param graph Component to calculate the demands for.
 */
DemandCalculator::DemandCalculator(LinkGraphComponent *graph) :
	max_distance(MapSizeX() + MapSizeY() + 1)
{
	CargoID cargo = graph->GetCargo();
	const LinkGraphSettings &settings = graph->GetSettings();

	this->accuracy = settings.accuracy;
	this->mod_dist = settings.demand_distance;
	if (this->mod_dist > 100) {
		/* increase effect of mod_dist > 100 */
		int over100 = this->mod_dist - 100;
		this->mod_dist = 100 + over100 * over100;
	}

	switch (settings.GetDistributionType(cargo)) {
		case DT_SYMMETRIC:
			this->CalcDemand<SymmetricScaler>(graph, SymmetricScaler(settings.demand_size));
			break;
		case DT_ASYMMETRIC:
			this->CalcDemand<AsymmetricScaler>(graph, AsymmetricScaler());
			break;
		default:
			NOT_REACHED();
			break;
	}
}
