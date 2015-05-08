/*******************************************************************************
 * c7a/api/dop_node.hpp
 *
 * Part of Project c7a.
 *
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_API_DOP_NODE_HEADER
#define C7A_API_DOP_NODE_HEADER

#include "dia_node.hpp"
#include "context.hpp"

namespace c7a {

template <typename T>
class DOpNode : public DIANode<T>
{
public:
    DOpNode(Context& ctx,
            const DIABaseVector& parents)
        : DIANode<T>(ctx, parents) { }

    virtual ~DOpNode() { }

    std::string ToString() override {
        // Create string
        std::string str 
            = std::string("[DOpNode]");
        return str;
    }
};

} // namespace c7a

#endif // !C7A_API_DOP_NODE_HEADER

/******************************************************************************/
