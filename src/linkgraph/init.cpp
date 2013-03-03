/** @file init.cpp Definition of initializing link graph handler. */

#include "../stdafx.h"
#include "init.h"

/**
 * Set up the node and edge annotations on the given job.
 * @param job Job to initialize.
 */
void InitHandler::Run(LinkGraphJob *job)
{
	LinkGraph &lg = job->Graph();
	uint size = lg.GetSize();
	job->Nodes().Resize(size);
	job->Edges().Resize(size, size);
	for (uint i = 0; i < size; ++i) {
		job->GetNode(i).undelivered_supply = lg.GetNode(i).supply;
		for (uint j = 0; j < size; ++j) {
			job->GetEdge(i, j).demand = 0;
		}
	}

}
