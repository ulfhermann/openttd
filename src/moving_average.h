/*
 * moving_average.h
 *
 *  Created on: Jan 13, 2010
 *      Author: alve
 */

#ifndef MOVING_AVERAGE_H_
#define MOVING_AVERAGE_H_

#include "stdafx.h"
#include "date_type.h"
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

extern const struct SaveLoad *GetMovingAverageDesc();

class MovingAverage : public MovingAveragePool::PoolItem<&_movingaverage_pool> {
private:
	uint value;
	uint length;

	void Register();

	friend void OnTick_MovingAverage();
	friend const SaveLoad *GetMovingAverageDesc();
public:
	MovingAverage(uint length = 0);

	inline uint Length() const
		{return this->length;}

	inline uint Value() const
		{return this->value;}

	inline uint Monthly() const
		{return this->value * DAY_TICKS * 30 / this->length;}

	inline uint Decrease()
		{assert(this->length > 0); return (this->value = this->value * (this->length - 1) / this->length);}
	inline uint Increase(uint increment)
		{return this->value += increment;}
};

/**
 * Iterate over all _valid_ moving averages from the given start
 * @param var   the variable used as "iterator"
 * @param start the moving average ID of the first packet to iterate over
 */
#define FOR_ALL_MOVING_AVERAGES_FROM(var, start) FOR_ALL_ITEMS_FROM(MovingAverage, movingaverage_index, var, start)

/**
 * Iterate over all _valid_ moving averages from the begin of the pool
 * @param var   the variable used as "iterator"
 */
#define FOR_ALL_MOVING_AVERAGES(var) FOR_ALL_MOVING_AVERAGES_FROM(var, 0)

typedef std::list<MovingAverageID> MovingAverageList;
typedef std::deque<MovingAverageList> MovingAverageRegistry;

extern MovingAverageRegistry _moving_averages;

void OnTick_MovingAverage();

#endif /* MOVING_AVERAGE_H_ */

