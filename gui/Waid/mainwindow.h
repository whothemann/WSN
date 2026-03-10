#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QMap>
#include <QSet>
#include <QPair>
#include <QString>
#include <QFrame>
#include <QLabel>
#include <QRegularExpression>

#include "qextserialport.h"
#include "qextserialenumerator.h"

#include "topologyview.h"
#include "devicetype.h"

namespace Ui {
class MainWindow;
}

/**
 * @brief Main application window.
 *
 * Responsibilities:
 * - Manage serial port open/close and read incoming log lines.
 * - Parse topology blocks (TOPO_BEGIN/TOPO_END) and render the topology graph.
 * - Parse emergency blocks (EMERG_BEGIN/EMERG_END) and color the bed indicators.
 * - Parse heartbeat lines at any time and show BPM per bed in the indicators.
 * - Clear BPM display for beds that are no longer present in the topology.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void changeEvent(QEvent *e) override;

private slots:
    void on_pushButton_open_clicked();
    void on_pushButton_close_clicked();
    void receive();

private:
    static QPair<QString, QString> normalizeLink(const QString &a, const QString &b)
    {
        return (a <= b) ? qMakePair(a, b) : qMakePair(b, a);
    }

    static QString prioToCssColor(int prio)
    {
        switch (prio) {
        case 0: return "#E53935";
        case 1: return "#FDD835";
        case 2: return "#43A047";
        default: return "#FFFFFF";
        }
    }

    void createBedIndicators();
    void resetBedIndicators();
    void resetBedBpmTexts();
    void updateBedIndicatorsFromMap(const QMap<int,int> &bedPrioMap);

    void setBedBpm(int bedNo, int bpm);
    void clearBedBpm(int bedNo);

    bool tryParseHeartbeatLine(const QString &line);

    /**
     * @brief Updates the set of beds that are currently present in the topology.
     *
     * After a TOPO_END, this extracts all BEDx nodes from the current snapshot.
     * Beds not present anymore will have their BPM display cleared.
     *
     * @param nodes Map of node role -> DeviceType from the current topology snapshot.
     */
    void syncBedsFromTopologySnapshot(const QMap<QString, DeviceType> &nodes);

private:
    Ui::MainWindow *ui;

    QextSerialPort port;
    QMessageBox error;

    TopologyView *topology = nullptr;

    bool m_inTopoBlock = false;
    QMap<QString, DeviceType>      m_nodesPending;
    QSet<QPair<QString, QString>>  m_linksPending;
    QMap<QString, DeviceType>      m_nodesApplied;
    QSet<QPair<QString, QString>>  m_linksApplied;

    bool m_inEmergBlock = false;
    QMap<int,int> m_emergPending;
    QMap<int,int> m_emergApplied;

    QFrame *m_bedFrame[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    QLabel *m_bedValue[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    QRegularExpression m_reHeartbeat;

    /**
     * @brief Tracks which beds (1..4) currently exist in the last applied topology.
     */
    QSet<int> m_activeBeds;
};

#endif // MAINWINDOW_H
