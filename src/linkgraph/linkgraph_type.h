/** @file linkgraph_type.h Declaration of link graph types used for cargo distribution. */

#ifndef LINKGRAPH_TYPE_H_
#define LINKGRAPH_TYPE_H_

typedef uint16 LinkGraphComponentID;
static const LinkGraphComponentID INVALID_LINKGRAPH_COMPONENT = UINT16_MAX;

typedef uint NodeID;
static const NodeID INVALID_NODE = UINT_MAX;

enum DistributionType {
	DT_BEGIN = 0,
	DT_SYMMETRIC = 0, ///< symmetric distribution. The same amount of cargo travels in each direction between each pair of nodes.
	DT_ASYMMETRIC,    ///< asymmetric distribution. Usually cargo will only travel in one direction.
	DT_MANUAL,        ///< manual distribution. No link graph calculations are run.
	DT_NUM = 3,
	DT_END = 3
};

/* It needs to be 8bits, because we save and load it as such
 * Define basic enum properties
 */
template <> struct EnumPropsT<DistributionType> : MakeEnumPropsT<DistributionType, byte, DT_BEGIN, DT_END, DT_NUM> {};
typedef TinyEnumT<DistributionType> DistributionTypeByte; // typedefing-enumification of DistributionType

#endif /* LINKGRAPH_TYPE_H_ */
