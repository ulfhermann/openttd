/** @file demands.h Declaration of demand calculating link graph handler. */

#ifndef DEMANDS_H_
#define DEMANDS_H_

#include "linkgraph.h"
#include "../cargo_type.h"
#include "../map_func.h"

/**
 * Scale various things according to symmetric/asymmetric distribution.
 */
class Scaler {
public:
	Scaler() : demand_per_node(0) {}

	void SetDemands(LinkGraphComponent * graph, NodeID from, NodeID to, uint demand_forw);
protected:
	uint demand_per_node; ///< Mean demand associated with each node.
};

/**
 * Scaler for symmetric distribution.
 */
class SymmetricScaler : public Scaler {
public:
	inline SymmetricScaler(uint mod_size) : mod_size(mod_size), supply_sum(0) {}

	/**
	 * Count a node's supply into the sum of supplies.
	 * @param node Node.
	 */
	inline void AddNode(const Node &node)
	{
		this->supply_sum += node.supply;
	}

	/**
	 * Calculate the mean demand per node using the sum of supplies.
	 * @param num_demands Number of accepting nodes.
	 */
	inline void SetDemandPerNode(uint num_demands)
	{
		this->demand_per_node = max(this->supply_sum / num_demands, 1U);
	}

	/**
	 * Get the effective supply of one node towards another one. In symmetric
	 * distribution the supply of the other node is weighed in.
	 * @param from The supplying node.
	 * @param to The receiving node.
	 * @return Effective supply.
	 */
	inline uint EffectiveSupply(const Node &from, const Node &to)
	{
		return max(from.supply * max(1U, to.supply) * this->mod_size / 100 / this->demand_per_node, 1U);
	}

	/**
	 * Check if there is any acceptance left for this node. In symmetric distribution
	 * nodes only accept anything if they also supply something. So if
	 * undelivered_supply == 0 at the node there isn't any demand left either.
	 * @param to The node to be checked.
	 */
	inline bool DemandLeft(Node &to)
	{
		return (to.supply == 0 || to.undelivered_supply > 0) && to.demand > 0;
	}

	void SetDemands(LinkGraphComponent *graph, NodeID from, NodeID to, uint demand_forw);

private:
	uint mod_size;   ///< Size modifier. Determines how much demands increase with the supply of the remote station
	uint supply_sum; ///< Sum of all supplies in the component.
};

/**
 * A scaler for asymmetric distribution.
 */
class AsymmetricScaler : public Scaler {
public:
	AsymmetricScaler() : demand_sum(0) {}

	/**
	 * Count a node's demand into the sum of demands.
	 * @param node The node to be counted.
	 */
	inline void AddNode(const Node &node)
	{
		this->demand_sum += node.demand;
	}

	/**
	 * Calculate the mean demand per node using the sum of demands.
	 * @param num_demands Number of accepting nodes.
	 */
	inline void SetDemandPerNode(uint num_demands)
	{
		this->demand_per_node = max(this->demand_sum / num_demands, (uint)1);
	}

	/**
	 * Get the effective supply of one node towards another one. In asymmetric
	 * distribution the demand of the other node is weighed in.
	 * @param from The supplying node.
	 * @param to The receiving node.
	 */
	inline uint EffectiveSupply(const Node &from, const Node &to)
	{
		return max(from.supply * to.demand / this->demand_per_node, (uint)1);
	}

	/**
	 * Check if there is any acceptance left for this node. In asymmetric distribution
	 * nodes always accept as long as their demand > 0.
	 * @param to The node to be checked.
	 */
	inline bool DemandLeft(Node &to) { return to.demand > 0; }

private:
	uint demand_sum; ///< Sum of all demands in the component.
};

/**
 * Calculate the demands. This class has a state, but is recreated for each
 * call to of DemandHandler::Run.
 */
class DemandCalculator {
public:
	DemandCalculator(LinkGraphComponent *graph);

private:
	int32 max_distance; ///< Maximum distance possible on the map.
	int32 mod_dist;     ///< Distance modifier, determines how much demands decrease with distance.
	int32 accuracy;     ///< Accuracy of the calculation.

	template<class Tscaler>
	void CalcDemand(LinkGraphComponent *graph, Tscaler scaler);
};

/**
 * Stateless, thread safe demand hander. Doesn't do anything but call DemandCalculator.
 */
class DemandHandler : public ComponentHandler {
public:

	/**
	 * Call the demand calculator on the given component.
	 * @param graph Component to calculate the demands for.
	 */
	virtual void Run(LinkGraphComponent *graph) { DemandCalculator c(graph); }

	/**
	 * Virtual destructor has to be defined because of virtual Run().
	 */
	virtual ~DemandHandler() {}
};

#endif /* DEMANDS_H_ */
