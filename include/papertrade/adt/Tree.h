#pragma once
//
// Tree.h — abstract general-tree ADT (spec §4 row 6).
//
// SectorTree implements this to model the Sector → Sub-sector → Company
// hierarchy for all 100 tickers. The interface is intentionally small: a general
// tree's shape is domain-specific, so the base only fixes the universal
// observers (size / empty / clear) and leaves node structure to the concrete
// class (Phase 5).
//
#include <cstddef>

namespace papertrade {

template <typename T>
class Tree {
public:
    virtual ~Tree() = default;

    virtual bool empty() const = 0;
    virtual std::size_t size() const = 0;  // number of nodes
    virtual void clear() = 0;
};

}  // namespace papertrade
