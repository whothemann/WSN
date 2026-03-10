#pragma once

#include <QGraphicsItem>

class NodeItem;

/**
 * @brief Graphics item representing a connection (edge) between two NodeItems.
 *
 * EdgeItem draws a line between a source node and a destination node.
 * It automatically updates its geometry when either node moves.
 */
class EdgeItem : public QGraphicsItem
{
public:
    /**
     * @brief Constructs an edge between two nodes.
     * @param source Pointer to the source NodeItem
     * @param dest   Pointer to the destination NodeItem
     */
    EdgeItem(NodeItem *source, NodeItem *dest);

    /**
     * @brief Returns the source node of the edge.
     * @return Pointer to the source NodeItem
     */
    NodeItem *sourceNode() const { return src; }

    /**
     * @brief Returns the destination node of the edge.
     * @return Pointer to the destination NodeItem
     */
    NodeItem *destNode() const { return dst; }

    /**
     * @brief Recalculates the edge geometry based on node positions.
     *
     * This method should be called whenever one of the connected nodes moves.
     */
    void adjust();

protected:
    /**
     * @brief Returns the bounding rectangle of the edge.
     * @return QRectF describing the bounding area used for painting and collision detection
     */
    QRectF boundingRect() const override;

    /**
     * @brief Paints the edge.
     * @param painter Painter used for drawing
     * @param option  Style options (unused)
     * @param widget  Optional widget (unused)
     */
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    NodeItem *src;
    NodeItem *dst;
    QPointF sourcePoint;
    QPointF destPoint;
};
