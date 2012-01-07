/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file moving_average.h Utility class for moving averages. */

#ifndef MOVING_AVERAGE_H_
#define MOVING_AVERAGE_H_

#include "settings_type.h"
#include "core/math_func.hpp"

/**
 * Class implementing moving average functionality. An instance of this class
 * can be used to get a meaningful (Monthly()) value from a moving average and
 * it can be used to do the decrease operation.
 * @tparam Tvalue Type supporting operator*(uint), operator/(uint),
 * operator*=(uint) and operator/=(uint) with the usual semantics.
 */
template<class Tvalue>
class MovingAverage {
protected:
	uint length;

public:
	/**
	 * Create a moving average.
	 * @param length Length to be used.
	 */
	inline MovingAverage(uint length) : length(length)
	{
		assert(this->length > 0);
	}

	/**
	 * Get the length of this moving average.
	 * @return Length.
	 */
	inline uint Length() const
	{
		return this->length;
	}

	/**
	 * Get the current average for 30 "length units" from the given value.
	 * Mind that no one forces you to decrease the average daily. If you
	 * don't this is not a real "monthly" value. In any case it's not really
	 * "monthly" as we don't account for months with different numbers of
	 * days than 30. It doesn't matter, though. The point is to get an
	 * average over a defined past timeframe.
	 * @param value Raw moving average.
	 * @return 30 "length unit" average.
	 */
	inline Tvalue Monthly(const Tvalue &value) const
	{
		return (value * 30) / (this->length);
	}

	/**
	 * Decrease the given value using this moving average.
	 * @param value Moving average value to be decreased.
	 * @return Decreased value.
	 */
	inline Tvalue &Decrease(Tvalue &value) const
	{
		return value = (value * this->length) / (this->length + 1);
	}
};

template<class Titem> void RunAverages();

#endif /* MOVING_AVERAGE_H_ */

