#pragma once

#include <QGraphicsView>
#include <QMap>
#include <QString>
#include "devicetype.h"

class NodeItem;

/**
 * @class TopologyView
 * @brief Graphics view for displaying and arranging a network topology.
 *
 * TopologyView manages a QGraphicsScene containing NodeItem and EdgeItem
 * objects. It provides methods to add nodes and links and periodically
 * updates their positions using a force-directed layout.
 */
class TopologyView : public QGraphicsView
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a TopologyView.
     * @param parent Optional parent widget.
     */
    explicit TopologyView(QWidget *parent = nullptr);

    /**
     * @brief Adds a node to the topology.
     * @param role Logical role or identifier of the node.
     * @param type Device type of the node.
     */
    void addNode(const QString &role, DeviceType type);

    /**
     * @brief Adds a link between two nodes.
     * @param srcRole Role of the source node.
     * @param dstRole Role of the destination node.
     */
    void addLink(const QString &srcRole, const QString &dstRole);

    /**
     * @brief Removes all nodes and links from the topology.
     */
    void clear();

private slots:
    /**
     * @brief Updates the layout of all nodes in the scene.
     */
    void updateLayout();

private:
    QGraphicsScene m_scene;
    QMap<QString, NodeItem*> m_nodes;
};
