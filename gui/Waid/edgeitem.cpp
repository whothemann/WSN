#include "edgeitem.h"
#include "nodeitem.h"
#include <QPainter>

/**
 * @brief Constructs an EdgeItem connecting two NodeItems.
 *
 * The edge registers itself at both nodes and is drawn behind them.
 *
 * @param source Pointer to the source NodeItem
 * @param dest   Pointer to the destination NodeItem
 */
EdgeItem::EdgeItem(NodeItem *source, NodeItem *dest)
    : src(source), dst(dest)
{
    src->addEdge(this);
    dst->addEdge(this);

    setZValue(-1);
    adjust();
}

/**
 * @brief Updates the cached geometry of the edge.
 *
 * This method recalculates the start and end points of the edge
 * based on the current positions of the connected nodes.
 */
void EdgeItem::adjust()
{
    if (!src || !dst)
        return;

    prepareGeometryChange();

    sourcePoint = mapFromItem(src, 0, 0);
    destPoint   = mapFromItem(dst, 0, 0);
}

/**
 * @brief Returns the bounding rectangle of the edge.
 *
 * The bounding rectangle is slightly expanded to ensure correct
 * repainting and hit detection.
 *
 * @return QRectF covering the painted line
 */
QRectF EdgeItem::boundingRect() const
{
    return QRectF(sourcePoint, destPoint)
        .normalized()
        .adjusted(-5, -5, 5, 5);
}

/**
 * @brief Paints the edge as a line between the connected nodes.
 *
 * @param painter Painter used for drawing
 * @param option  Style options (unused)
 * @param widget  Optional widget (unused)
 */
void EdgeItem::paint(QPainter *painter,
                     const QStyleOptionGraphicsItem *,
                     QWidget *)
{
    if (!src || !dst)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(Qt::blue, 2));
    painter->drawLine(sourcePoint, destPoint);
}
