#include "topologywidget.h"
#include <QPainter>
#include <QMap>

TopologyWidget::TopologyWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 400);
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
}

void TopologyWidget::setLinks(const QSet<QPair<int,int>> &links)
{
    m_links = links;
    update();
}

void TopologyWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // --- relative node positions (like sketch) ---
    QMap<int, QPoint> pos;

    pos[1] = QPoint(width() * 0.2, height() * 0.15);
    pos[2] = QPoint(width() * 0.8, height() * 0.15);

    pos[3] = QPoint(width() * 0.25, height() * 0.45);
    pos[4] = QPoint(width() * 0.75, height() * 0.45);

    pos[5] = QPoint(width() * 0.5, height() * 0.70);

    pos[6] = QPoint(width() * 0.2, height() * 0.85);
    pos[7] = QPoint(width() * 0.8, height() * 0.85);

    // --- draw links ---
    p.setPen(QPen(Qt::blue, 2));
    for (const auto &l : m_links) {
        if (pos.contains(l.first) && pos.contains(l.second)) {
            p.drawLine(pos[l.first], pos[l.second]);
        }
    }

    // --- draw nodes ---
    for (int id : pos.keys()) {
        QColor color = Qt::green;

        if (id == 1) color = Qt::gray;
        if (id == 2) color = Qt::green;
        if (id == 3) color = QColor(255,165,0); // orange
        if (id == 4) color = Qt::red;
        if (id == 5) color = Qt::blue;

        drawNode(p, pos[id], id, color);
    }
}

void TopologyWidget::drawNode(QPainter &p, QPoint center, int id, QColor color)
{
    int scale = qMin(width(), height());
    int deviceW = scale * 0.18;
    int deviceH = scale * 0.22;
    int r = scale * 0.04;

    // --- device ---
    QRect device(center.x() - deviceW/2,
                 center.y() - deviceH/2,
                 deviceW,
                 deviceH);

    p.setPen(QPen(Qt::black, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(device, 8, 8);

    // --- status circle ---
    QPoint circleCenter(center.x(), device.bottom() - r);
    p.setBrush(color);
    p.setPen(Qt::black);
    p.drawEllipse(circleCenter, r, r);

    // --- ID ---
    p.setPen(Qt::white);
    p.drawText(circleCenter + QPoint(-5, 6),
               QString::number(id));
}
