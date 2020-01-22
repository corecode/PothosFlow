// Copyright (c) 2013-2016 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include "GraphObjects/GraphEndpoint.hpp"
#include "GraphObjects/GraphObject.hpp"
#include <QHash>

GraphConnectableKey::GraphConnectableKey(const QString &id, const GraphConnectableDirection direction):
    id(id),
    direction(direction)
{
    return;
}

bool GraphConnectableKey::isInput(void) const
{
    return direction == GRAPH_CONN_INPUT or direction == GRAPH_CONN_SLOT;
}

bool GraphConnectableKey::isValid(void) const
{
    return not id.isEmpty();
}

bool operator==(const GraphConnectableKey &key0, const GraphConnectableKey &key1)
{
    return key0.id == key1.id and key0.direction == key1.direction;
}

GraphConnectionEndpoint::GraphConnectionEndpoint(const QPointer<GraphObject> &obj, const GraphConnectableKey &key)
{
    _obj = obj;
    _key = key;
}

const QPointer<GraphObject> &GraphConnectionEndpoint::getObj(void) const
{
    return _obj;
}

const GraphConnectableKey &GraphConnectionEndpoint::getKey(void) const
{
    return _key;
}

GraphConnectableAttrs GraphConnectionEndpoint::getConnectableAttrs(void) const
{
    return _obj->getConnectableAttrs(_key);
}

bool GraphConnectionEndpoint::isValid(void) const
{
    return not _obj.isNull() and _key.isValid();
}

bool operator==(const GraphConnectionEndpoint &ep0, const GraphConnectionEndpoint &ep1)
{
    return ep0.getObj() == ep1.getObj() and ep0.getKey() == ep1.getKey();
}

std::hash<GraphConnectableKey>::value_type std::hash<GraphConnectableKey>::operator()(argument_type const& s) const noexcept
{
    return qHash(s.id) ^ (qHash(s.direction) << 1);
}

std::hash<GraphConnectionEndpoint>::value_type std::hash<GraphConnectionEndpoint>::operator()(argument_type const& s) const noexcept
{
    return std::hash<GraphConnectableKey>()(s.getKey()) ^
        (std::hash<size_t>()(size_t(s.getObj().data())) << 1);
}
