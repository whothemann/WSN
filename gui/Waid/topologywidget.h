#pragma once

#include <QWidget>
#include <QSet>
#include <QPair>

class TopologyWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TopologyWidget(QWidget *parent = nullptr);

    void setLinks(const QSet<QPair<int,int>> &links);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QSet<QPair<int,int>> m_links;

    void drawNode(QPainter &p, QPoint center, int id, QColor color);
};
