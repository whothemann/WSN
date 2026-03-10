#pragma once

#include <QGraphicsItem>
#include <QVector>
#include <QString>

#include "devicetype.h"

class EdgeItem;

/**
 * @class NodeItem
 * @brief Graphical representation of a device node in a QGraphicsScene.
 *
 * NodeItem represents a network device (e.g., gateway, access point, bed, nurse)
 * and participates in a force-directed layout. Nodes can be pinned to fixed
 * positions (e.g., hubs) and can freeze after converging to speed up the layout.
 *
 * Nodes are connected via EdgeItem objects which are adjusted whenever this node
 * moves (including mouse moves).
 */
class NodeItem : public QGraphicsItem
{
public:
    /**
     * @brief Constructs a NodeItem.
     * @param role Logical role/identifier of the node (e.g., "BED1", "ACCESS2").
     * @param type Device type determining appearance and labeling.
     */
    explicit NodeItem(const QString &role, DeviceType type);

    /**
     * @brief Pins or unpins the node.
     *
     * A pinned node does not participate in the force-directed layout and keeps
     * its position fixed (unless moved explicitly by code).
     *
     * @param pinned True to pin the node, false to allow movement.
     */
    void setPinned(bool pinned);

    /**
     * @brief Checks whether the node is pinned.
     * @return True if the node is pinned.
     */
    bool isPinned() const;

    /**
     * @brief Returns the logical role of the node.
     * @return Role string.
     */
    QString role() const;

    /**
     * @brief Returns the device type of the node.
     * @return DeviceType enum value.
     */
    DeviceType deviceType() const;

    /**
     * @brief Adds an edge connected to this node.
     * @param edge Pointer to the edge to add.
     */
    void addEdge(EdgeItem *edge);

    /**
     * @brief Returns all edges connected to this node.
     * @return Vector of edge pointers.
     */
    QVector<EdgeItem*> edges() const;

    /**
     * @brief Calculates forces acting on the node for force-directed layouting.
     *
     * Computes repulsion from other nodes and attraction along edges to determine
     * a new target position. Pinned or frozen nodes keep their current position.
     */
    void calculateForces();

    /**
     * @brief Applies the previously calculated position.
     * @return True if the position changed, false otherwise.
     */
    bool advancePosition();

protected:
    /**
     * @brief Returns the bounding rectangle of the node.
     * @return Bounding rectangle in item coordinates.
     */
    QRectF boundingRect() const override;

    /**
     * @brief Paints the node depending on its device type.
     * @param painter Painter used for rendering.
     * @param option Style options (unused).
     * @param widget Optional widget (unused).
     */
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    /**
     * @brief Handles item state changes.
     *
     * When the node position changes, the node is unfrozen (layout continues) and
     * all connected edges are adjusted.
     *
     * @param change Type of change.
     * @param value Associated value.
     * @return Result from the base class implementation.
     */
    QVariant itemChange(QGraphicsItem::GraphicsItemChange change,
                        const QVariant &value) override;

private:
    QString m_role;                /**< Node role/identifier (e.g., "GATEWAY", "ACCESS2", "BED3"). */
    DeviceType m_type;             /**< Device type that drives drawing and labeling. */
    QVector<EdgeItem*> m_edges;    /**< Edges connected to this node. */
    QPointF newPos;                /**< Target position computed by the layout step. */

    bool m_frozen = false;         /**< True if node is frozen due to convergence. */
    int  m_stableCounter = 0;      /**< Counts consecutive small movement steps (for freezing). */
    bool m_pinned = false;         /**< True if node is pinned (not moved by the layout). */
};
