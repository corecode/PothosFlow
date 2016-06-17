// Copyright (c) 2013-2016 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include "GraphObjects/GraphWidget.hpp"
#include "GraphObjects/GraphBlock.hpp"
#include "GraphObjects/GraphWidgetContainer.hpp"
#include "GraphEditor/Constants.hpp"
#include "GraphEditor/GraphDraw.hpp"
#include "GraphEditor/GraphEditor.hpp"
#include <Pothos/Exception.hpp>
#include <QGraphicsProxyWidget>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QMetaObject>
#include <QMetaMethod>
#include <vector>
#include <iostream>
#include <cassert>

/***********************************************************************
 * GraphWidget private container
 **********************************************************************/
struct GraphWidget::Impl
{
    Impl(QGraphicsItem *parent):
        container(new GraphWidgetContainer()),
        graphicsWidget(new QGraphicsProxyWidget(parent))
    {
        graphicsWidget->setWidget(container);
    }

    ~Impl(void)
    {
        graphicsWidget->setWidget(nullptr);
    }

    QPointer<GraphBlock> block;

    GraphWidgetContainer *container;
    QGraphicsProxyWidget *graphicsWidget;

    QVariant widgetState;
    QMetaObject::Connection stateChangedConnection;
    QMetaObject::Connection stateRestoreConnection;
};

/***********************************************************************
 * GraphWidget Implementation
 **********************************************************************/
GraphWidget::GraphWidget(QObject *parent):
    GraphObject(parent),
    _impl(new Impl(this))
{
    this->setFlag(QGraphicsItem::ItemIsMovable);
    connect(_impl->container, SIGNAL(resized(void)), this, SLOT(handleWidgetResized(void)));
}

GraphWidget::~GraphWidget(void)
{
    return;
}

void GraphWidget::setGraphBlock(GraphBlock *block)
{
    //can only set block once
    assert(_impl);
    assert(not _impl->block);

    _impl->block = block;
    connect(block, SIGNAL(destroyed(QObject *)), this, SLOT(handleBlockDestroyed(QObject *)));
    connect(block, SIGNAL(IDChanged(const QString &)), this, SLOT(handleBlockIdChanged(const QString &)));
    connect(block, SIGNAL(evalDoneEvent(void)), this, SLOT(handleBlockEvalDone(void)));
    this->handleBlockIdChanged(block->getId()); //init value
}

GraphBlock *GraphWidget::getGraphBlock(void) const
{
    return _impl->block;
}

bool GraphWidget::containerHasFocus(void) const
{
    auto fw = _impl->container->focusWidget();
    return fw != nullptr and fw->hasFocus();
}

void GraphWidget::handleBlockDestroyed(QObject *)
{
    //an endpoint was destroyed, schedule for deletion
    //however, the top level code should handle this deletion
    _impl->container->setWidget(nullptr);
    this->flagForDelete();
}

void GraphWidget::handleWidgetResized(void)
{
    auto editor = this->draw()->getGraphEditor();
    editor->handleStateChange(GraphState("transform-scale", tr("Resize %1").arg(this->getId())));
}

void GraphWidget::handleBlockIdChanged(const QString &id)
{
    _impl->container->setGripLabel(id);
}

QPainterPath GraphWidget::shape(void) const
{
    return _impl->graphicsWidget->shape();
}

QVariant GraphWidget::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == QGraphicsItem::ItemSelectedHasChanged)
    {
        _impl->container->setSelected(this->isSelected());
    }

    return QGraphicsItem::itemChange(change, value);
}

void GraphWidget::handleBlockEvalDone(void)
{
    //the widget could have changed when a block eval completes
    auto graphWidget = _impl->block->getGraphWidget();
    auto oldWidget = _impl->container->widget();
    _impl->container->setWidget(graphWidget);

    //no change, ignore logic below
    if (oldWidget == graphWidget) return;

    //disconnect old widget
    if (_impl->stateChangedConnection)
    {
        this->disconnect(_impl->stateChangedConnection);
        _impl->stateChangedConnection = QMetaObject::Connection();
    }
    if (_impl->stateRestoreConnection)
    {
        this->disconnect(_impl->stateRestoreConnection);
        _impl->stateRestoreConnection = QMetaObject::Connection();
    }

    //connect new widget
    if (graphWidget == nullptr) return;
    auto mo = graphWidget->metaObject();
    if (mo->indexOfSignal(QMetaObject::normalizedSignature("stateChanged(QVariant)").constData()) == -1) return;
    if (mo->indexOfSlot(QMetaObject::normalizedSignature("restoreState(QVariant)").constData()) == -1) return;
    _impl->stateChangedConnection = this->connect(graphWidget, SIGNAL(stateChanged(const QVariant &)), this, SLOT(handleWidgetStateChanged(const QVariant &)));
    _impl->stateRestoreConnection = this->connect(this, SIGNAL(restoreWidgetState(const QVariant &)), graphWidget, SLOT(restoreState(const QVariant &)));
    emit this->restoreWidgetState(_impl->widgetState); //emit state to new widget (also done in deserialize)
}

void GraphWidget::handleWidgetStateChanged(const QVariant &state)
{
    //stash the state -- will actually be saved by serialize
    _impl->widgetState = state;
}

/***********************************************************************
 * serialize/deserialize hooks
 **********************************************************************/
Poco::JSON::Object::Ptr GraphWidget::serialize(void) const
{
    auto obj = GraphObject::serialize();
    obj->set("what", std::string("Widget"));
    obj->set("blockId", _impl->block->getId().toStdString());
    obj->set("width", _impl->graphicsWidget->size().width());
    obj->set("height", _impl->graphicsWidget->size().height());

    //save the widget state to JSON
    if (_impl->widgetState.isValid())
    {
        QByteArray data;
        QDataStream ds(&data, QIODevice::WriteOnly);
        ds << _impl->widgetState;
        obj->set("state", data.toBase64().toStdString());
    }

    return obj;
}

void GraphWidget::deserialize(Poco::JSON::Object::Ptr obj)
{
    auto editor = this->draw()->getGraphEditor();

    //locate the associated block
    auto blockId = QString::fromStdString(obj->getValue<std::string>("blockId"));
    auto graphObj = editor->getObjectById(blockId, GRAPH_BLOCK);
    if (graphObj == nullptr) throw Pothos::NotFoundException("GraphWidget::deserialize()", "cant resolve block with ID: '"+blockId.toStdString()+"'");
    auto graphBlock = dynamic_cast<GraphBlock *>(graphObj);
    assert(graphBlock != nullptr);
    this->setGraphBlock(graphBlock);

    if (obj->has("width") and obj->has("height"))
    {
        _impl->graphicsWidget->resize(
            obj->getValue<int>("width"),
            obj->getValue<int>("height"));
    }

    //restore the widget state from JSON
    const auto state = obj->optValue<std::string>("state", "");
    if (state.empty()) _impl->widgetState.clear();
    else
    {
        auto data = QByteArray::fromStdString(state);
        data = QByteArray::fromBase64(data);
        QDataStream ds(&data, QIODevice::ReadOnly);
        ds >> _impl->widgetState;
    }

    //emit the current state to the widget when connected
    if (_impl->stateRestoreConnection and _impl->widgetState.isValid())
    {
        emit this->restoreWidgetState(_impl->widgetState);
    }

    GraphObject::deserialize(obj);
}
