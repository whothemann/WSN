#include "nodeitem.h"
#include "edgeitem.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QtMath>

/**
 * @brief Constructs a NodeItem with a given role and device type.
 * @param role Logical role/name used to derive labels (e.g., AP number, bed number).
 * @param type Device type that determines node appearance and labeling.
 */
NodeItem::NodeItem(const QString &role, DeviceType type)
    : m_role(role),
    m_type(type)
{
    setFlag(ItemIsMovable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setCacheMode(DeviceCoordinateCache);
    setZValue(1);

    newPos = pos();
}

/**
 * @brief Pins or unpins the node.
 * @param pinned True to pin the node, false to allow movement.
 */
void NodeItem::setPinned(bool pinned)
{
    m_pinned = pinned;
    if (m_pinned) {
        m_frozen = false;
        m_stableCounter = 0;
        newPos = pos();
    }
}

/**
 * @brief Checks whether the node is pinned.
 * @return True if the node is pinned.
 */
bool NodeItem::isPinned() const
{
    return m_pinned;
}

/**
 * @brief Returns the logical role of the node.
 * @return Role string.
 */
QString NodeItem::role() const
{
    return m_role;
}

/**
 * @brief Returns the device type of the node.
 * @return DeviceType enum value.
 */
DeviceType NodeItem::deviceType() const
{
    return m_type;
}

/**
 * @brief Adds an edge connected to this node.
 * @param edge Pointer to the edge to add.
 */
void NodeItem::addEdge(EdgeItem *edge)
{
    if (!edge) return;
    m_edges << edge;
}

/**
 * @brief Returns all edges connected to this node.
 * @return Vector of pointers to connected edges.
 */
QVector<EdgeItem*> NodeItem::edges() const
{
    return m_edges;
}

/**
 * @brief Returns the bounding rectangle of the node item.
 * @return Bounding rectangle in item coordinates.
 */
QRectF NodeItem::boundingRect() const
{
    return QRectF(-60, -60, 120,120);
}

/**
 * @brief Paints the node.
 *
 * The node color and label depend on the device type:
 * - Gateway: dark gray, label "GW"
 * - AccessPoint: green, label "AP" or "AP<n>" derived from the role
 * - Nurse: red, label "N"
 * - Bed: orange, label "B" or "B<n>" derived from the role
 * - Default: light gray, label "?"
 *
 * @param p Painter used for rendering.
 * @param option Unused style options.
 * @param widget Unused widget pointer.
 */
void NodeItem::paint(QPainter *p,
                     const QStyleOptionGraphicsItem *option,
                     QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    p->setRenderHint(QPainter::Antialiasing);

    QColor color;
    QString label;

    QFont f = p->font();
    f.setBold(true);
    f.setPointSize(12);
    p->setFont(f);


    switch (m_type) {
    case DeviceType::Gateway:
        color = Qt::darkGray;
        label = "GW";
        break;

    case DeviceType::AccessPoint: {
        color = Qt::green;
        const int apNo = accessNumberFromRole(m_role);
        if (apNo > 0)       label = QString("AP%1").arg(apNo);
        else if (apNo == 0) label = "AP";
        else                label = "AP";
        break;
    }

    case DeviceType::Nurse:
        color = Qt::red;
        label = "N";
        break;

    case DeviceType::Bed: {
        color = QColor(255, 165, 0);
        const int bedNo = bedNumberFromRole(m_role);
        label = (bedNo > 0) ? QString("B%1").arg(bedNo) : "B";
        break;
    }

    default:
        color = Qt::lightGray;
        label = "?";
        break;
    }

    p->setBrush(color);
    p->setPen(Qt::black);
    p->drawEllipse(-45, -45, 90, 90);

    p->setPen(Qt::black);
    p->drawText(QRectF(-35, -22, 70, 44), Qt::AlignCenter, label);
}

/**
 * @brief Computes the next position of the node for a force-directed layout.
 *
 * Pinned or frozen nodes (and nodes without a scene) keep their current position.
 * The layout combines:
 * - A hard minimum distance repulsion against other nodes.
 * - A spring attraction along connected edges (Hooke's law) toward a rest length.
 * - Damping and clamping to limit movement per step.
 *
 * If the node remains nearly stationary for a number of iterations, it is frozen
 * to speed up convergence.
 */
void NodeItem::calculateForces()
{
    if (!scene() || m_pinned || m_frozen) {
        newPos = pos();
        return;
    }

    const double MIN_DIST   = 185.0;
    const double LINK_REST  = 180.0;
    const double K_SPRING   = 0.0065;
    const double EDGE_CLAMP = 1.2;

    QPointF correction(0, 0);
    QPointF attraction(0, 0);

    const QList<QGraphicsItem*> allItems = scene()->items();
    for (QGraphicsItem *item : allItems) {
        NodeItem *node = qgraphicsitem_cast<NodeItem*>(item);
        if (!node || node == this) continue;

        QPointF vec = pos() - node->pos();
        double dist = std::hypot(vec.x(), vec.y());
        if (dist < 0.1) dist = 0.1;

        if (dist < MIN_DIST) {
            const double overlap = MIN_DIST - dist;
            correction += (vec / dist) * overlap * 0.5;
        }
    }

    for (EdgeItem *edge : m_edges) {
        if (!edge) continue;

        NodeItem *other =
            edge->sourceNode() == this ? edge->destNode()
                                       : edge->sourceNode();
        if (!other) continue;

        QPointF d = other->pos() - pos();
        double dist = std::hypot(d.x(), d.y());
        if (dist < 0.1) dist = 0.1;

        const QPointF dir = d / dist;
        const double f = K_SPRING * (dist - LINK_REST);

        QPointF push = dir * f;
        push.setX(qBound(-EDGE_CLAMP, push.x(), EDGE_CLAMP));
        push.setY(qBound(-EDGE_CLAMP, push.y(), EDGE_CLAMP));

        attraction += push;
    }

    QPointF delta = (correction * 0.20) + (attraction * 1.0);

    delta.setX(qBound(-0.9, delta.x(), 0.9));
    delta.setY(qBound(-0.9, delta.y(), 0.9));

    const double len = std::hypot(delta.x(), delta.y());

    if (len < 0.05) {
        m_stableCounter++;
    } else {
        m_stableCounter = 0;
    }

    if (m_stableCounter > 20) {
        m_frozen = true;
        newPos = pos();
        return;
    }

    newPos = pos() + delta;
}

/**
 * @brief Applies the position computed by calculateForces().
 * @return True if the position changed, false otherwise.
 */
bool NodeItem::advancePosition()
{
    if (newPos == pos())
        return false;

    setPos(newPos);
    return true;
}

/**
 * @brief Handles item changes and updates connected edges when the node moves.
 *
 * When the position changes, the node is unfrozen and its stability counter is reset.
 * All connected edges are adjusted to follow the new node position.
 *
 * @param change Type of change.
 * @param value New value associated with the change.
 * @return Result from the base class implementation.
 */
QVariant NodeItem::itemChange(GraphicsItemChange change,
                              const QVariant &value)
{
    if (change == ItemPositionHasChanged) {
        m_frozen = false;
        m_stableCounter = 0;

        for (EdgeItem *edge : m_edges) {
            if (edge) edge->adjust();
        }
    }

    return QGraphicsItem::itemChange(change, value);
}
