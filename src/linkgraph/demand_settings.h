/** @file demand_settings.h Declaration of distribution types for demand calculation. */

#ifndef DEMAND_SETTINGS_H_
#define DEMAND_SETTINGS_H_

enum DistributionType {
	DT_BEGIN = 0,
	DT_SYMMETRIC = 0,
	DT_ANTISYMMETRIC,
	DT_UNHANDLED,
	DT_NUM = 3,
	DT_END = 3
};

/** It needs to be 8bits, because we save and load it as such
 * Define basic enum properties */
template <> struct EnumPropsT<DistributionType> : MakeEnumPropsT<DistributionType, byte, DT_BEGIN, DT_END, DT_NUM> {};
typedef TinyEnumT<DistributionType> DistributionTypeByte; // typedefing-enumification of DistributionType

#endif /* DEMAND_SETTINGS_H_ */
