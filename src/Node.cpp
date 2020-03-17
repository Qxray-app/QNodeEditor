#include "Node.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "ConnectionState.hpp"
#include "FlowScene.hpp"
#include "NodeDataModel.hpp"
#include "NodeGraphicsObject.hpp"

#include <QtCore/QObject>
#include <iostream>
#include <utility>

using QtNodes::Node;
using QtNodes::NodeData;
using QtNodes::NodeDataModel;
using QtNodes::NodeDataType;
using QtNodes::NodeGeometry;
using QtNodes::NodeGraphicsObject;
using QtNodes::NodeState;
using QtNodes::PortIndex;
using QtNodes::PortType;

Node::Node(std::unique_ptr<NodeDataModel> &&dataModel)
    : _uid(QUuid::createUuid()), _nodeDataModel(std::move(dataModel)), _nodeState(_nodeDataModel), _nodeGeometry(_nodeDataModel),
      _nodeGraphicsObject(nullptr)
{
    _nodeGeometry.recalculateSize();
    // propagate data: model => node
    connect(_nodeDataModel.get(), &NodeDataModel::dataUpdated, this, &Node::onDataUpdated);
    connect(_nodeDataModel.get(), &NodeDataModel::embeddedWidgetSizeUpdated, this, &Node::onNodeSizeUpdated);
    // connect(_nodeDataModel.get(), &NodeDataModel::portCountChanged,
    //        this, &Node::onPortCountChanged);
}

Node::~Node() = default;

QJsonObject Node::save() const
{
    QJsonObject nodeJson;
    nodeJson["id"] = _uid.toString();
    nodeJson["model"] = _nodeDataModel->save();
    QJsonObject obj;
    obj["x"] = _nodeGraphicsObject->pos().x();
    obj["y"] = _nodeGraphicsObject->pos().y();
    nodeJson["position"] = obj;
    return nodeJson;
}

void Node::restore(QJsonObject const &json)
{
    _uid = QUuid(json["id"].toString());
    QJsonObject positionJson = json["position"].toObject();
    QPointF point(positionJson["x"].toDouble(), positionJson["y"].toDouble());
    _nodeGraphicsObject->setPos(point);
    _nodeDataModel->restore(json["model"].toObject());
}

QUuid Node::id() const
{
    return _uid;
}

void Node::reactToPossibleConnection(PortType reactingPortType, NodeDataType const &reactingDataType, QPointF const &scenePoint)
{
    QTransform const t = _nodeGraphicsObject->sceneTransform();
    //
    QPointF nodePoint = t.inverted().map(scenePoint);
    _nodeGeometry.setDraggingPosition(nodePoint);
    //
    _nodeGraphicsObject->update();
    _nodeState.setReaction(NodeState::REACTING, reactingPortType, reactingDataType);
}

void Node::resetReactionToConnection()
{
    _nodeState.setReaction(NodeState::NOT_REACTING);
    _nodeGraphicsObject->update();
}

NodeGraphicsObject const &Node::nodeGraphicsObject() const
{
    return *_nodeGraphicsObject.get();
}

NodeGraphicsObject &Node::nodeGraphicsObject()
{
    return *_nodeGraphicsObject.get();
}

void Node::setGraphicsObject(std::unique_ptr<NodeGraphicsObject> &&graphics)
{
    _nodeGraphicsObject = std::move(graphics);
    _nodeGeometry.recalculateSize();
}

NodeGeometry &Node::nodeGeometry()
{
    return _nodeGeometry;
}

NodeGeometry const &Node::nodeGeometry() const
{
    return _nodeGeometry;
}

NodeState const &Node::nodeState() const
{
    return _nodeState;
}

NodeState &Node::nodeState()
{
    return _nodeState;
}

NodeDataModel *Node::nodeDataModel() const
{
    return _nodeDataModel.get();
}

void Node::propagateData(PortIndex inPortIndex) const
{
    NodeState const &state = nodeState();
    NodeState::ConnectionPtrSet connections = state.connections(PortType::In, inPortIndex);
    std::vector<std::shared_ptr<NodeData>> nodeData;
    nodeData.reserve(connections.size());

    for (const auto &connection : connections)
    {
        Connection *c = connection.second;
        Node *outNode = c->getNode(PortType::Out);
        PortIndex outNodeIndex = c->getPortIndex(PortType::Out);
        std::shared_ptr<NodeData> outData = outNode->nodeDataModel()->outData(outNodeIndex);
        TypeConverter converter = c->getTypeConverter();

        if (converter != nullptr)
        {
            outData = converter(outData);
        }

        nodeData.push_back(outData);
    }

    _nodeDataModel->setInData(std::move(nodeData), inPortIndex);
    // Recalculate the nodes visuals. A data change can result in the node taking
    // more space than before, so this forces a recalculate+repaint on the affected
    // node
    _nodeGraphicsObject->setGeometryChanged();
    _nodeGeometry.recalculateSize();
    _nodeGraphicsObject->update();
    _nodeGraphicsObject->moveConnections();
    recalculateVisuals();
}

void Node::onDataUpdated(PortIndex index)
{
    auto nodeData = _nodeDataModel->outData(index);
    auto connections = _nodeState.connections(PortType::Out, index);

    for (auto const &c : connections) c.second->propagateData(nodeData);
}

void Node::onNodeSizeUpdated()
{
    if (nodeDataModel()->embeddedWidget())
    {
        nodeDataModel()->embeddedWidget()->adjustSize();
    }

    nodeGeometry().recalculateSize();

    for (PortType type : { PortType::In, PortType::Out })
    {
        for (auto &conn_set : nodeState().getEntries(type))
        {
            for (auto &pair : conn_set)
            {
                Connection *conn = pair.second;
                conn->getConnectionGraphicsObject().move();
            }
        }
    }
}

void Node::onPortCountChanged()
{
    // Remove connections
    for (auto portType : { PortType::In, PortType::Out })
    {
        auto &entries = _nodeState.getEntries(portType);
        int oldCount = static_cast<int>(entries.size());
        int newCount = _nodeDataModel->nPorts(portType);

        for (PortIndex index = newCount; index < oldCount; ++index)
        {
            auto connections = entries[index];

            for (auto const &c : connections)
            {
                Q_EMIT killConnection(*c.second);
            }
        }
    }
    _nodeState.updatePortCount(_nodeDataModel->nPorts(PortType::In), _nodeDataModel->nPorts(PortType::Out));
    nodeGeometry().updatePortCount();
    recalculateVisuals();
}

void Node::recalculateVisuals() const
{
    _nodeGraphicsObject->setGeometryChanged();
    _nodeGeometry.recalculateSize();
    _nodeGraphicsObject->update();
    _nodeGraphicsObject->moveConnections();
}
