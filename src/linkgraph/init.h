/** @file init.h Declaration of initializing link graph handler. */

#ifndef INIT_H
#define INIT_H

#include "linkgraph.h"

/**
 * Stateless, thread safe initialization hander. Initializes node and edge
 * annotations.
 */
class InitHandler : public ComponentHandler {
public:

	virtual void Run(LinkGraphJob *job);

	/**
	 * Virtual destructor has to be defined because of virtual Run().
	 */
	virtual ~InitHandler() {}
};

#endif /* INIT_H */
