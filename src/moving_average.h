/*
 * moving_average.h
 *
 *  Created on: Jan 13, 2010
 *      Author: alve
 */

#ifndef MOVING_AVERAGE_H_
#define MOVING_AVERAGE_H_

#include "stdafx.h"
#include "core/pool_type.hpp"
#include <deque>
#include <list>

/** Unique identifier for a single moving average. */
typedef uint32 MovingAverageID;
class MovingAverage;

/** Type of the pool for moving averages. */
typedef Pool<MovingAverage, MovingAverageID, 1024, 1048576, true, false> MovingAveragePool;
/** The actual pool with moving averages */
extern MovingAveragePool _movingaverage_pool;


class MovingAverage : public MovingAveragePool::PoolItem<&_movingaverage_pool> {
private:
	typedef std::list<MovingAverageID> MovingAverageList;
	typedef std::deque<MovingAverageList> MovingAverageDeque;

	static MovingAverageDeque _moving_averages;

	uint value;

	void InsertIntoDeque();

public:
	static void Tick();

	uint length;

	MovingAverage(uint length);

	inline uint Value() const
		{return this->value;}

	inline uint Decrease()
		{assert(this->length > 0); return (this->value = this->value * (this->length - 1) / this->length);}
	inline uint Increase(uint increment)
		{return this->value += increment;}

};

#endif /* MOVING_AVERAGE_H_ */

