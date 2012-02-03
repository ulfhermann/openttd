/** @file demands.h Declaration of normalizing link graph handler. */

#ifndef NORMALIZE_H_
#define NORMALIZE_H_

#include "linkgraph.h"

/**
 * Normalize the linkgraph. This class has a state, but is recreated for each
 * call to of NormalizeHandler::Run.
 */
class Normalizer {
public:
	Normalizer(LinkGraphComponent *graph);

private:
	void ReroutePassby(LinkGraphComponent *graph, NodeID node_id, NodeID export_id, NodeID other_id);
};

/**
 * Stateless, thread safe demand hander. Doesn't do anything but call Normalizer.
 */
class NormalizeHandler : public ComponentHandler {
public:

	/**
	 * Call the normalizer on the given component.
	 * @param graph Component to be normalized.
	 */
	virtual void Run(LinkGraphComponent *graph) { Normalizer c(graph); }

	/**
	 * Virtual destructor has to be defined because of virtual Run().
	 */
	virtual ~NormalizeHandler() {}
};

#endif /* NORMALIZE_H_ */
