#include "topologyview.h"
#include "nodeitem.h"
#include "edgeitem.h"

#include <QTimer>
#include <QRandomGenerator>
#include <QtMath>
#include <algorithm>

/**
 * @brief Computes a spring force between two points using Hooke's law.
 * @param from Source position.
 * @param to Target position.
 * @param restLen Rest length of the spring.
 * @param k Spring constant.
 * @return Force vector applied to the source point.
 */
static QPointF springForce(const QPointF &from, const QPointF &to, double restLen, double k)
{
    QPointF d = to - from;
    double dist = std::hypot(d.x(), d.y());
    if (dist < 0.001) dist = 0.001;

    QPointF dir = d / dist;
    double f = k * (dist - restLen);
    return dir * f;
}

/**
 * @brief Constructs the topology view.
 * @param parent Optional parent widget.
 *
 * Initializes the graphics scene, rendering settings, and a timer that
 * periodically updates the layout.
 */
TopologyView::TopologyView(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(&m_scene);
    setRenderHint(QPainter::Antialiasing);

    setSceneRect(-400, -300, 800, 600);
    setAlignment(Qt::AlignCenter);

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TopologyView::updateLayout);
    timer->start(30);
}

/**
 * @brief Adds a node to the topology.
 * @param role Logical role or identifier of the node.
 * @param type Device type of the node.
 *
 * If a node with the same role already exists or the role is empty,
 * the call has no effect.
 */
void TopologyView::addNode(const QString &role, DeviceType type)
{
    if (role.isEmpty()) return;
    if (m_nodes.contains(role)) return;

    NodeItem *node = new NodeItem(role, type);
    m_scene.addItem(node);
    m_nodes[role] = node;

    node->setPos(
        QRandomGenerator::global()->bounded(-250, 250),
        QRandomGenerator::global()->bounded(-200, 200)
        );
}

/**
 * @brief Adds a link between two existing nodes.
 * @param srcRole Role of the source node.
 * @param dstRole Role of the destination node.
 *
 * The link is only created if both nodes exist.
 */
void TopologyView::addLink(const QString &srcRole, const QString &dstRole)
{
    if (!m_nodes.contains(srcRole) || !m_nodes.contains(dstRole))
        return;

    m_scene.addItem(new EdgeItem(m_nodes[srcRole], m_nodes[dstRole]));
}

/**
 * @brief Clears the topology.
 *
 * Removes all nodes and edges from the scene and resets internal state.
 */
void TopologyView::clear()
{
    m_scene.clear();
    m_nodes.clear();
}

/**
 * @brief Updates the layout of the topology.
 *
 * The update consists of:
 * - Pinning and positioning the gateway node.
 * - Detecting and pinning access point nodes.
 * - Computing additional spring forces toward hubs.
 * - Applying the standard force-directed layout.
 * - Applying additional clustering forces.
 */
void TopologyView::updateLayout()
{
    NodeItem *gw = m_nodes.contains("GATEWAY") ? m_nodes["GATEWAY"] : nullptr;

    const QPointF gwPos(400,0);
    if (gw) {
        gw->setPinned(true);
        gw->setPos(gwPos);
    }

    QList<NodeItem*> aps;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        const QString &name = it.key();
        if (name.startsWith("ACCESS")) {
            aps.append(it.value());
        }
    }

    std::sort(aps.begin(), aps.end(), [](NodeItem *a, NodeItem *b){
        if (!a || !b) return false;
        return a->role() < b->role();
    });

    const double xRight = -400.0;
    if (!aps.isEmpty()) {
        if (aps.size() == 1) {
            aps[0]->setPinned(true);
            aps[0]->setPos(QPointF(xRight, -20));
        } else {
            const double yTop = -120.0;
            const double yBot =  120.0;
            for (int i = 0; i < aps.size(); ++i) {
                double t = (aps.size() == 1) ? 0.5
                                             : static_cast<double>(i) / static_cast<double>(aps.size() - 1);
                double y = yTop + t * (yBot - yTop);
                aps[i]->setPinned(true);
                aps[i]->setPos(QPointF(xRight, y));
            }
        }
    }

    QMap<NodeItem*, QPointF> extra;

    const double kSpring    = 0.008;
    const double restToHub  = 180.0;
    const double restNormal = 260.0;
    const double clampMax   = 3.0;

    auto isHub = [&](NodeItem *n) -> bool {
        if (!n) return false;
        if (n == gw) return true;
        return aps.contains(n);
    };

    for (NodeItem *node : m_nodes) {
        if (!node || node->isPinned())
            continue;

        QPointF sum(0, 0);

        for (EdgeItem *edge : node->edges()) {
            NodeItem *other =
                edge->sourceNode() == node ? edge->destNode() : edge->sourceNode();
            if (!other) continue;

            const bool otherIsHub = isHub(other);
            const double restLen = otherIsHub ? restToHub : restNormal;

            sum += springForce(node->pos(), other->pos(), restLen, kSpring);
        }

        sum.setX(qBound(-clampMax, sum.x(), clampMax));
        sum.setY(qBound(-clampMax, sum.y(), clampMax));
        extra[node] = sum;
    }

    for (NodeItem *node : m_nodes)
        node->calculateForces();

    for (NodeItem *node : m_nodes)
        node->advancePosition();

    for (auto it = extra.begin(); it != extra.end(); ++it) {
        NodeItem *n = it.key();
        if (!n || n->isPinned()) continue;
        n->setPos(n->pos() + it.value());
    }
}
