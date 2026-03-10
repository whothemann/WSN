#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    topology = new TopologyView(this);
    ui->topologyLayout->addWidget(topology);

    createBedIndicators();
    resetBedIndicators();
    resetBedBpmTexts();

    m_reHeartbeat = QRegularExpression(
        R"((?:\[\s*HEARTBEAT\s*\]|HEARTBEAT)\s*BED\s*([0-9]+)\s*BPM\s*:?\s*([0-9]+))",
        QRegularExpression::CaseInsensitiveOption
        );

    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    for (const QextPortInfo &info : ports) {
        if (info.portName.contains("ACM")) {
            ui->comboBox_Interface->addItem(info.portName);
        }
    }

    if (ui->comboBox_Interface->count() == 0) {
        ui->textEdit_Status->append("No USB ports found.\nConnect a device and restart.");
    }

    ui->pushButton_close->setEnabled(false);
}

MainWindow::~MainWindow()
{
    if (port.isOpen()) {
        port.close();
    }
    delete ui;
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
}

void MainWindow::createBedIndicators()
{
    QWidget *container = new QWidget(this);

    QHBoxLayout *row = new QHBoxLayout(container);
    row->setContentsMargins(20, 12, 20, 12);
    row->setSpacing(18);

    row->addStretch(1);

    QFont titleFont;
    titleFont.setPointSize(16);
    titleFont.setBold(true);

    QFont valueFont;
    valueFont.setPointSize(14);
    valueFont.setBold(false);

    for (int bed = 1; bed <= 4; bed++) {
        QFrame *f = new QFrame(container);
        f->setObjectName(QString("frameBed%1").arg(bed));
        f->setFrameShape(QFrame::StyledPanel);
        f->setMinimumSize(200, 95);

        QLabel *title = new QLabel(QString("BED%1").arg(bed), f);
        title->setAlignment(Qt::AlignCenter);
        title->setFont(titleFont);

        QLabel *value = new QLabel("BPM: -", f);
        value->setAlignment(Qt::AlignCenter);
        value->setFont(valueFont);

        QVBoxLayout *inner = new QVBoxLayout(f);
        inner->setContentsMargins(10, 8, 10, 8);
        inner->setSpacing(6);
        inner->addWidget(title);
        inner->addWidget(value);

        f->setStyleSheet(
            "background: #FFFFFF;"
            "border: 3px solid #222222;"
            "border-radius: 10px;"
            );

        row->addWidget(f);

        m_bedFrame[bed] = f;
        m_bedValue[bed] = value;
    }

    row->addStretch(1);

    ui->topologyLayout->addWidget(container);
}

void MainWindow::resetBedIndicators()
{
    for (int bed = 1; bed <= 4; bed++) {
        if (!m_bedFrame[bed]) continue;

        m_bedFrame[bed]->setStyleSheet(
            "background: #FFFFFF;"
            "border: 3px solid #222222;"
            "border-radius: 10px;"
            );
    }
}

void MainWindow::resetBedBpmTexts()
{
    for (int bed = 1; bed <= 4; bed++) {
        clearBedBpm(bed);
    }
}

void MainWindow::updateBedIndicatorsFromMap(const QMap<int,int> &bedPrioMap)
{
    resetBedIndicators();

    for (auto it = bedPrioMap.begin(); it != bedPrioMap.end(); ++it) {
        const int bedNo = it.key();
        const int prio  = it.value();

        if (bedNo < 1 || bedNo > 4) continue;
        if (!m_bedFrame[bedNo]) continue;

        const QString bg = prioToCssColor(prio);
        m_bedFrame[bedNo]->setStyleSheet(
            QString(
                "background: %1;"
                "border: 3px solid #222222;"
                "border-radius: 10px;"
                ).arg(bg)
            );
    }
}

void MainWindow::setBedBpm(int bedNo, int bpm)
{
    if (bedNo < 1 || bedNo > 4) return;
    if (!m_bedValue[bedNo]) return;

    m_bedValue[bedNo]->setText(QString("BPM: %1").arg(bpm));
}

void MainWindow::clearBedBpm(int bedNo)
{
    if (bedNo < 1 || bedNo > 4) return;
    if (!m_bedValue[bedNo]) return;

    m_bedValue[bedNo]->setText("BPM: -");
}

bool MainWindow::tryParseHeartbeatLine(const QString &line)
{
    const QRegularExpressionMatch m = m_reHeartbeat.match(line);
    if (!m.hasMatch()) {
        return false;
    }

    bool okBed = false;
    bool okBpm = false;

    const int bedNo = m.captured(1).toInt(&okBed);
    const int bpm   = m.captured(2).toInt(&okBpm);

    if (!okBed || !okBpm) {
        return false;
    }

    setBedBpm(bedNo, bpm);
    return true;
}

void MainWindow::syncBedsFromTopologySnapshot(const QMap<QString, DeviceType> &nodes)
{
    QSet<int> newActiveBeds;

    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        if (it.value() != DeviceType::Bed) {
            continue;
        }

        const int bedNo = bedNumberFromRole(it.key());
        if (bedNo >= 1 && bedNo <= 4) {
            newActiveBeds.insert(bedNo);
        }
    }

    for (int bed = 1; bed <= 4; ++bed) {
        if (!newActiveBeds.contains(bed)) {
            clearBedBpm(bed);
        }
    }

    m_activeBeds = newActiveBeds;
}

void MainWindow::on_pushButton_open_clicked()
{
    const QString portName = "/dev/" + ui->comboBox_Interface->currentText();

    port.setQueryMode(QextSerialPort::EventDriven);
    port.setPortName(portName);
    port.setBaudRate(BAUD115200);
    port.setFlowControl(FLOW_OFF);
    port.setParity(PAR_NONE);
    port.setDataBits(DATA_8);
    port.setStopBits(STOP_1);

    if (!port.open(QIODevice::ReadWrite)) {
        error.setText("Unable to open port!");
        error.exec();
        return;
    }

    connect(&port, SIGNAL(readyRead()), this, SLOT(receive()));

    ui->pushButton_open->setEnabled(false);
    ui->pushButton_close->setEnabled(true);
    ui->comboBox_Interface->setEnabled(false);

    ui->textEdit_Status->append("Opened port: " + portName);

    if (topology) topology->clear();
    resetBedIndicators();
    resetBedBpmTexts();

    m_activeBeds.clear();

    m_inTopoBlock = false;
    m_nodesPending.clear();
    m_linksPending.clear();
    m_nodesApplied.clear();
    m_linksApplied.clear();

    m_inEmergBlock = false;
    m_emergPending.clear();
    m_emergApplied.clear();
}

void MainWindow::on_pushButton_close_clicked()
{
    if (port.isOpen()) {
        port.close();
    }

    if (topology) {
        topology->clear();
    }

    resetBedIndicators();
    resetBedBpmTexts();

    m_activeBeds.clear();

    m_inTopoBlock = false;
    m_nodesPending.clear();
    m_linksPending.clear();
    m_nodesApplied.clear();
    m_linksApplied.clear();

    m_inEmergBlock = false;
    m_emergPending.clear();
    m_emergApplied.clear();

    ui->pushButton_open->setEnabled(true);
    ui->pushButton_close->setEnabled(false);
    ui->comboBox_Interface->setEnabled(true);

    ui->textEdit_Status->append("Port closed. Topology cleared.");
}

void MainWindow::receive()
{
    static QString line;
    char ch;

    while (port.getChar(&ch)) {
        line.append(ch);

        if (ch != '\n') {
            continue;
        }

        line = line.trimmed();
        if (line.isEmpty()) {
            line.clear();
            continue;
        }

        ui->textEdit_Status->append(line);

        tryParseHeartbeatLine(line);

        if (line == "TOPO_BEGIN") {
            m_inTopoBlock = true;
            m_nodesPending.clear();
            m_linksPending.clear();
            line.clear();
            continue;
        }

        if (line == "TOPO_END") {
            m_inTopoBlock = false;

            const bool changed =
                (m_nodesPending != m_nodesApplied) ||
                (m_linksPending != m_linksApplied);

            if (changed) {
                if (topology) {
                    topology->clear();

                    for (auto it = m_nodesPending.begin(); it != m_nodesPending.end(); ++it) {
                        topology->addNode(it.key(), it.value());
                    }

                    for (const auto &l : m_linksPending) {
                        topology->addLink(l.first, l.second);
                    }
                }

                m_nodesApplied = m_nodesPending;
                m_linksApplied = m_linksPending;

                ui->textEdit_Status->append("[GUI] Topology updated.");
            } else {
                ui->textEdit_Status->append("[GUI] Topology unchanged (skip rebuild).");
            }

            /* Always sync bed presence from the latest snapshot */
            syncBedsFromTopologySnapshot(m_nodesPending);

            line.clear();
            continue;
        }

        if (line == "EMERG_BEGIN") {
            m_inEmergBlock = true;
            m_emergPending.clear();
            line.clear();
            continue;
        }

        if (line == "EMERG_END") {
            m_inEmergBlock = false;

            const bool changed = (m_emergPending != m_emergApplied);
            if (changed) {
                updateBedIndicatorsFromMap(m_emergPending);
                m_emergApplied = m_emergPending;
                ui->textEdit_Status->append("[GUI] Bed indicators updated.");
            } else {
                ui->textEdit_Status->append("[GUI] Bed indicators unchanged.");
            }

            line.clear();
            continue;
        }

        if (!m_inTopoBlock && !m_inEmergBlock) {
            line.clear();
            continue;
        }

        if (m_inEmergBlock) {
            const QStringList parts = line.split(" ", Qt::SkipEmptyParts);

            if (parts.size() >= 3 && parts[0] == "EMERG") {
                const QString label = parts[1].trimmed();

                bool okPrio = false;
                const int prio = parts[2].toInt(&okPrio);
                if (!okPrio) {
                    line.clear();
                    continue;
                }

                if (label.startsWith("BED")) {
                    bool okBed = false;
                    const int bedNo = label.mid(3).toInt(&okBed);
                    if (okBed && bedNo >= 1 && bedNo <= 4) {
                        m_emergPending[bedNo] = prio;
                    }
                }
            }

            line.clear();
            continue;
        }

        if (m_inTopoBlock) {
            const QStringList parts = line.split(" ", Qt::SkipEmptyParts);

            if (parts.size() >= 3 && parts[0] == "LINK") {
                const QString parentRole = parts[1].trimmed();
                const QString childRole  = parts[2].trimmed();

                if (!parentRole.isEmpty() && !childRole.isEmpty() && parentRole != childRole) {
                    m_nodesPending[parentRole] = deviceTypeFromString(parentRole);
                    m_nodesPending[childRole]  = deviceTypeFromString(childRole);
                    m_linksPending.insert(normalizeLink(parentRole, childRole));
                }
            }

            line.clear();
            continue;
        }

        line.clear();
    }
}
