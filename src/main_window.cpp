#include "main_window.h"

#include "ai_command_bridge.h"

#include <QAbstractItemView>
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

namespace
{
void repolish(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

void setSendButtonSending(QPushButton *button, bool sending)
{
    button->setText(sending ? QStringLiteral("停止") : QStringLiteral("发送"));
    button->setProperty("tone", sending ? QStringLiteral("destructive") : QStringLiteral("secondary"));
    button->setProperty("active", sending);
    repolish(button);
}

void configureComboBox(QComboBox *comboBox)
{
    auto *view = new QListView(comboBox);
    view->setObjectName(QStringLiteral("comboPopup"));
    view->setFrameShape(QFrame::NoFrame);
    view->setUniformItemSizes(true);
    view->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    comboBox->setView(view);
    comboBox->view()->window()->setWindowFlag(Qt::NoDropShadowWindowHint, true);
}

QString compactHexText(const QString &text, int maxNibbles)
{
    QString hex;
    hex.reserve(maxNibbles);
    for (const QChar ch : text) {
        if (ch.isDigit() || (ch.toUpper() >= QLatin1Char('A') && ch.toUpper() <= QLatin1Char('F'))) {
            hex.append(ch.toUpper());
            if (hex.size() >= maxNibbles) {
                break;
            }
        }
    }
    return hex;
}

QString formatFrameDataText(const QString &text)
{
    const QString hex = compactHexText(text, 16);
    QStringList bytes;
    for (int i = 0; i < hex.size(); i += 2) {
        bytes.append(hex.mid(i, 2));
    }
    return bytes.join(QLatin1Char(' '));
}

int frameDataLength(const QString &formattedText)
{
    const QString hex = compactHexText(formattedText, 16);
    return hex.size() / 2;
}

QByteArray frameDataBytes(const QString &formattedText)
{
    const QString hex = compactHexText(formattedText, 16);
    QByteArray data;
    data.reserve(hex.size() / 2);
    for (int i = 0; i + 1 < hex.size(); i += 2) {
        bool ok = false;
        const auto value = static_cast<char>(hex.mid(i, 2).toUInt(&ok, 16));
        if (ok) {
            data.append(value);
        }
    }
    return data;
}

QString frameIdText(unsigned int frameId, bool extendedFrame)
{
    return QStringLiteral("%1").arg(frameId, extendedFrame ? 8 : 3, 16, QLatin1Char('0')).toUpper();
}

QString frameDataText(const QByteArray &data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (const char byte : data) {
        bytes.append(QStringLiteral("%1").arg(static_cast<unsigned char>(byte), 2, 16, QLatin1Char('0')).toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}

QString receiveTimeText(qint64 receivedAtMs)
{
    if (receivedAtMs <= 0) {
        return QString();
    }
    return QDateTime::fromMSecsSinceEpoch(receivedAtMs).toString(QStringLiteral("HH:mm:ss.zzz"));
}

QTableWidgetItem *makeTableItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

MainWindow::MainWindow(DeviceController *controller, AiCommandBridge *aiBridge, QWidget *parent)
    : QMainWindow(parent)
    , controller_(controller)
    , aiBridge_(aiBridge)
{
    setWindowTitle(QStringLiteral("ZWXCanTools"));
    setMinimumSize(1040, 660);
    buildUi();
    applyAppStyle();

    connect(controller_, &DeviceController::stateChanged, this, &MainWindow::refreshState);
    connect(controller_, &DeviceController::operationMessage, this, [this](const QString &message) {
        messageLabel_->setText(message);
    });
    connect(aiBridge_, &AiCommandBridge::bridgeMessage, this, [this](const QString &message) {
        messageLabel_->setText(message);
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &MainWindow::stopReceiveTimers);

    refreshState(controller_->state());
}

void MainWindow::stopReceiveTimers()
{
    for (auto *timer : {can0ReceiveTimer_, can1ReceiveTimer_}) {
        if (timer != nullptr) {
            timer->stop();
        }
    }
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(28, 28, 40, 34);
    rootLayout->setSpacing(28);

    auto *sidebar = makeCardPanel(root);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(292);
    sidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 14, 14, 14);
    sidebarLayout->setSpacing(10);

    menuLayout_ = new QVBoxLayout();
    menuLayout_->setSpacing(8);
    sidebarLayout->addLayout(menuLayout_);
    sidebarLayout->addStretch();

    messageLabel_ = new QLabel(QStringLiteral("就绪"), sidebar);
    messageLabel_->setObjectName(QStringLiteral("messageLabel"));
    messageLabel_->setWordWrap(true);
    messageLabel_->setMinimumHeight(42);
    sidebarLayout->addWidget(messageLabel_);

    stack_ = new QStackedWidget(root);
    stack_->setObjectName(QStringLiteral("contentStack"));

    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(stack_, 1);
    setCentralWidget(root);

    buildSidebar();
    buildDevicePage();
    buildCanSendPage(0);
    buildCanReceivePage(0);
    buildCanSendPage(1);
    buildCanReceivePage(1);
    loadSendFrames();
}

void MainWindow::buildSidebar()
{
    auto *deviceButton = makeMenuButton(QStringLiteral("设备连接"), QStringLiteral("D"), this);
    deviceButton->setChecked(true);
    connect(deviceButton, &QPushButton::clicked, this, [this]() {
        selectPage(0, qobject_cast<QPushButton *>(sender()));
    });
    menuButtons_.append(deviceButton);
    menuLayout_->addWidget(deviceButton);

    const QList<QPair<QString, QString>> items = {
        {QStringLiteral("CAN0发送"), QStringLiteral("TX")},
        {QStringLiteral("CAN0接收"), QStringLiteral("RX")},
        {QStringLiteral("CAN1发送"), QStringLiteral("TX")},
        {QStringLiteral("CAN1接收"), QStringLiteral("RX")},
    };

    for (int i = 0; i < items.size(); ++i) {
        auto *button = makeMenuButton(items.at(i).first, items.at(i).second, this);
        const int pageIndex = i + 1;
        connect(button, &QPushButton::clicked, this, [this, pageIndex, button]() {
            selectPage(pageIndex, button);
        });
        menuButtons_.append(button);
        menuLayout_->addWidget(button);
    }
}

void MainWindow::selectPage(int pageIndex, QPushButton *selectedButton)
{
    stack_->setCurrentIndex(pageIndex);
    for (auto *button : menuButtons_) {
        button->setChecked(button == selectedButton);
    }
}

void MainWindow::buildDevicePage()
{
    auto *page = new QWidget(stack_);
    page->setObjectName(QStringLiteral("contentPage"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(36, 24, 28, 36);
    pageLayout->setSpacing(18);

    auto *headerRow = new QHBoxLayout();
    auto *heading = new QLabel(QStringLiteral("设备连接"), page);
    heading->setObjectName(QStringLiteral("pageTitle"));
    deviceStatusPill_ = makeStatusPill(QStringLiteral("未打开"), page);
    headerRow->addWidget(heading);
    headerRow->addStretch();
    headerRow->addWidget(deviceStatusPill_);
    pageLayout->addLayout(headerRow);

    auto *connectPanel = makeCardPanel(page);
    connectPanel->setObjectName(QStringLiteral("connectPanel"));
    auto *connectLayout = new QVBoxLayout(connectPanel);
    connectLayout->setContentsMargins(0, 0, 0, 0);
    connectLayout->setSpacing(0);

    auto addSettingRow = [connectPanel, connectLayout](const QString &text, QWidget *control) {
        auto *row = new QWidget(connectPanel);
        row->setObjectName(QStringLiteral("settingRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(22, 14, 18, 14);
        rowLayout->setSpacing(18);

        auto *label = new QLabel(text, row);
        label->setObjectName(QStringLiteral("fieldLabel"));
        rowLayout->addWidget(label);
        rowLayout->addStretch();
        rowLayout->addWidget(control);
        connectLayout->addWidget(row);
    };

    deviceTypeCombo_ = new QComboBox(connectPanel);
    deviceTypeCombo_->addItems({QStringLiteral("USBCAN-II"), QStringLiteral("USBCAN-I"), QStringLiteral("CANFD-200U")});
    configureComboBox(deviceTypeCombo_);
    deviceTypeCombo_->setFixedWidth(190);
    addSettingRow(QStringLiteral("类型"), deviceTypeCombo_);

    deviceIndexSpin_ = new QSpinBox(connectPanel);
    deviceIndexSpin_->setRange(0, 3);
    deviceIndexSpin_->setFixedWidth(128);
    addSettingRow(QStringLiteral("索引"), deviceIndexSpin_);

    auto *openRow = new QWidget(connectPanel);
    openRow->setObjectName(QStringLiteral("settingRow"));
    auto *openRowLayout = new QHBoxLayout(openRow);
    openRowLayout->setContentsMargins(22, 14, 18, 14);
    openDeviceButton_ = makePrimaryButton(QStringLiteral("打开设备"), openRow);
    openDeviceButton_->setProperty("tone", QStringLiteral("accent"));
    auto *openLabel = new QLabel(QStringLiteral("设备"), openRow);
    openLabel->setObjectName(QStringLiteral("fieldLabel"));
    openRowLayout->addWidget(openLabel);
    openRowLayout->addStretch();
    openRowLayout->addWidget(openDeviceButton_);
    connectLayout->addWidget(openRow);
    pageLayout->addWidget(connectPanel);

    deviceTreePanel_ = makeCardPanel(page);
    deviceTreePanel_->setObjectName(QStringLiteral("deviceTreePanel"));
    auto *treeLayout = new QVBoxLayout(deviceTreePanel_);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(0);

    auto *deviceRow = new QHBoxLayout();
    deviceNameLabel_ = new QLabel(QStringLiteral("USBCAN-II  设备0"), deviceTreePanel_);
    deviceNameLabel_->setObjectName(QStringLiteral("treeDeviceLabel"));
    deviceRow->setContentsMargins(22, 16, 18, 16);
    deviceRow->addWidget(deviceNameLabel_);
    deviceRow->addStretch();
    treeLayout->addLayout(deviceRow);

    auto addChannel = [this, treeLayout](int index, QPushButton **buttonSlot, QLabel **statusSlot) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(22, 14, 18, 14);
        row->setSpacing(14);
        auto *label = new QLabel(QStringLiteral("通道%1").arg(index), deviceTreePanel_);
        label->setObjectName(QStringLiteral("treeChannelLabel"));
        auto *status = makeStatusPill(QStringLiteral("未启动"), deviceTreePanel_);
        auto *button = makePrimaryButton(QStringLiteral("启动"), deviceTreePanel_);
        button->setProperty("tone", QStringLiteral("secondary"));
        row->addWidget(label);
        row->addWidget(status);
        row->addStretch();
        row->addWidget(button);
        treeLayout->addLayout(row);
        *buttonSlot = button;
        *statusSlot = status;
    };

    addChannel(0, &channel0StartButton_, &channel0Status_);
    addChannel(1, &channel1StartButton_, &channel1Status_);
    pageLayout->addWidget(deviceTreePanel_);
    pageLayout->addStretch();

    connect(deviceTypeCombo_, &QComboBox::currentTextChanged, controller_, &DeviceController::setDeviceType);
    connect(deviceIndexSpin_, &QSpinBox::valueChanged, controller_, &DeviceController::setDeviceIndex);
    connect(openDeviceButton_, &QPushButton::clicked, this, [this]() {
        if (controller_->state().deviceOpen) {
            controller_->closeDevice();
        } else {
            controller_->openDevice();
        }
    });
    connect(channel0StartButton_, &QPushButton::clicked, this, [this]() { showStartDialog(0); });
    connect(channel1StartButton_, &QPushButton::clicked, this, [this]() { showStartDialog(1); });

    stack_->addWidget(page);
}

void MainWindow::buildCanSendPage(int channelIndex)
{
    auto *page = new QWidget(stack_);
    page->setObjectName(QStringLiteral("contentPage"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(36, 24, 28, 36);
    pageLayout->setSpacing(18);

    auto *headerRow = new QHBoxLayout();
    auto *heading = new QLabel(QStringLiteral("CAN%1发送").arg(channelIndex), page);
    heading->setObjectName(QStringLiteral("pageTitle"));
    auto *addButton = makePrimaryButton(QStringLiteral("+"), page);
    addButton->setObjectName(QStringLiteral("addButton"));
    addButton->setProperty("tone", QStringLiteral("accent"));
    addButton->setFixedSize(46, 46);
    headerRow->addWidget(heading);
    headerRow->addStretch();
    headerRow->addWidget(addButton);
    pageLayout->addLayout(headerRow);

    auto *listPanel = makeCardPanel(page);
    listPanel->setObjectName(QStringLiteral("sendListPanel"));
    auto *listPanelLayout = new QVBoxLayout(listPanel);
    listPanelLayout->setContentsMargins(0, 0, 0, 0);
    listPanelLayout->setSpacing(0);

    auto *scroll = new QScrollArea(listPanel);
    scroll->setObjectName(QStringLiteral("sendScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *listContent = new QWidget(scroll);
    listContent->setObjectName(QStringLiteral("sendListContent"));
    auto *listLayout = new QVBoxLayout(listContent);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    auto *emptyLabel = new QLabel(QStringLiteral("暂无发送帧"), listContent);
    emptyLabel->setObjectName(QStringLiteral("emptyStateLabel"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setMinimumHeight(160);
    listLayout->addWidget(emptyLabel);
    listLayout->addStretch();

    scroll->setWidget(listContent);
    listPanelLayout->addWidget(scroll);
    pageLayout->addWidget(listPanel, 1);

    if (channelIndex == 0) {
        can0SendListLayout_ = listLayout;
        can0SendEmptyLabel_ = emptyLabel;
    } else {
        can1SendListLayout_ = listLayout;
        can1SendEmptyLabel_ = emptyLabel;
    }

    connect(addButton, &QPushButton::clicked, this, [this, channelIndex]() {
        showAddSendFrameDialog(channelIndex);
    });

    stack_->addWidget(page);
}

void MainWindow::buildCanReceivePage(int channelIndex)
{
    auto *page = new QWidget(stack_);
    page->setObjectName(QStringLiteral("contentPage"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(36, 24, 28, 36);
    pageLayout->setSpacing(18);

    auto *heading = new QLabel(QStringLiteral("CAN%1接收").arg(channelIndex), page);
    heading->setObjectName(QStringLiteral("pageTitle"));
    pageLayout->addWidget(heading);

    auto *panel = makeCardPanel(page);
    panel->setObjectName(QStringLiteral("receivePanel"));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 18, 18, 18);
    panelLayout->setSpacing(14);

    auto *toolbar = new QWidget(panel);
    toolbar->setObjectName(QStringLiteral("receiveToolbar"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(12);

    auto *filterLabel = new QLabel(QStringLiteral("过滤ID"), toolbar);
    filterLabel->setObjectName(QStringLiteral("fieldLabel"));
    auto *filterEdit = new QLineEdit(toolbar);
    filterEdit->setObjectName(QStringLiteral("filterEdit"));
    filterEdit->setPlaceholderText(QStringLiteral("留空显示全部"));
    filterEdit->setFixedWidth(220);
    filterEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,8}")), filterEdit));
    auto *clearButton = new QPushButton(QStringLiteral("清除"), toolbar);
    clearButton->setObjectName(QStringLiteral("secondaryButton"));
    toolbarLayout->addWidget(filterLabel);
    toolbarLayout->addWidget(filterEdit);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(clearButton);
    panelLayout->addWidget(toolbar);

    auto *table = new QTableWidget(panel);
    table->setObjectName(QStringLiteral("receiveTable"));
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("接收时间"), QStringLiteral("ID"), QStringLiteral("类型"),
         QStringLiteral("格式"), QStringLiteral("DLC"), QStringLiteral("数据")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    panelLayout->addWidget(table, 1);

    pageLayout->addWidget(panel, 1);

    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, channelIndex]() {
        pollReceiveFrames(channelIndex);
    });
    timer->start();

    connect(filterEdit, &QLineEdit::textChanged, this, [this, channelIndex]() {
        refreshReceiveTable(channelIndex);
    });
    connect(clearButton, &QPushButton::clicked, this, [this, channelIndex]() {
        clearReceiveFrames(channelIndex);
    });

    if (channelIndex == 0) {
        can0ReceiveTable_ = table;
        can0ReceiveFilterEdit_ = filterEdit;
        can0ReceiveTimer_ = timer;
    } else {
        can1ReceiveTable_ = table;
        can1ReceiveFilterEdit_ = filterEdit;
        can1ReceiveTimer_ = timer;
    }

    stack_->addWidget(page);
}

void MainWindow::pollReceiveFrames(int channelIndex)
{
    const auto &state = controller_->state();
    const bool channelStarted = channelIndex == 0 ? state.channel0Started : state.channel1Started;
    if (!state.deviceOpen || !channelStarted) {
        return;
    }

    QVector<CanRxFrame> receivedFrames;
    if (!controller_->receiveFrames(channelIndex, 32, &receivedFrames) || receivedFrames.isEmpty()) {
        return;
    }

    auto &frames = channelIndex == 0 ? can0ReceiveFrames_ : can1ReceiveFrames_;
    for (auto frame : receivedFrames) {
        frame.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
        frames.append(frame);
        while (frames.size() > 99) {
            frames.removeFirst();
        }
    }
    refreshReceiveTable(channelIndex);
}

void MainWindow::refreshReceiveTable(int channelIndex)
{
    auto *table = channelIndex == 0 ? can0ReceiveTable_ : can1ReceiveTable_;
    auto *filterEdit = channelIndex == 0 ? can0ReceiveFilterEdit_ : can1ReceiveFilterEdit_;
    const auto &frames = channelIndex == 0 ? can0ReceiveFrames_ : can1ReceiveFrames_;
    if (table == nullptr || filterEdit == nullptr) {
        return;
    }

    const QString filterText = compactHexText(filterEdit->text(), 8);
    bool hasFilter = !filterText.isEmpty();
    bool filterOk = false;
    const unsigned int filterId = filterText.toUInt(&filterOk, 16);
    hasFilter = hasFilter && filterOk;

    table->setUpdatesEnabled(false);
    table->setRowCount(0);
    int row = 0;
    for (int i = 0; i < frames.size(); ++i) {
        const auto &frame = frames.at(i);
        if (hasFilter && frame.frameId != filterId) {
            continue;
        }

        table->insertRow(row);
        table->setItem(row, 0, makeTableItem(QString::number(i + 1)));
        table->setItem(row, 1, makeTableItem(receiveTimeText(frame.receivedAtMs)));
        table->setItem(row, 2, makeTableItem(frameIdText(frame.frameId, frame.extendedFrame)));
        table->setItem(row, 3, makeTableItem(frame.extendedFrame ? QStringLiteral("扩展帧") : QStringLiteral("标准帧")));
        table->setItem(row, 4, makeTableItem(frame.remoteFrame ? QStringLiteral("远程帧") : QStringLiteral("数据帧")));
        table->setItem(row, 5, makeTableItem(QString::number(frame.data.size())));
        table->setItem(row, 6, makeTableItem(frame.remoteFrame ? QString() : frameDataText(frame.data)));
        ++row;
    }
    table->setUpdatesEnabled(true);
}

void MainWindow::clearReceiveFrames(int channelIndex)
{
    auto &frames = channelIndex == 0 ? can0ReceiveFrames_ : can1ReceiveFrames_;
    frames.clear();
    refreshReceiveTable(channelIndex);
    messageLabel_->setText(QStringLiteral("CAN%1接收表格已清除").arg(channelIndex));
}

void MainWindow::showAddSendFrameDialog(int channelIndex)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("sendFrameDialog"));
    dialog.setWindowTitle(QStringLiteral("新增发送帧"));
    dialog.setModal(true);
    dialog.resize(560, 480);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(30, 28, 30, 26);
    layout->setSpacing(18);

    auto *title = new QLabel(QStringLiteral("CAN%1 新增发送帧").arg(channelIndex), &dialog);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);

    auto *formPanel = makeCardPanel(&dialog);
    formPanel->setObjectName(QStringLiteral("dialogFormPanel"));
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(0);

    auto addDialogRow = [formPanel, formLayout](const QString &text, QWidget *control) {
        auto *row = new QWidget(formPanel);
        row->setObjectName(QStringLiteral("settingRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(22, 13, 18, 13);
        rowLayout->setSpacing(18);
        auto *label = new QLabel(text, row);
        label->setObjectName(QStringLiteral("fieldLabel"));
        rowLayout->addWidget(label);
        rowLayout->addStretch();
        rowLayout->addWidget(control);
        formLayout->addWidget(row);
    };

    auto *idEdit = new QLineEdit(&dialog);
    idEdit->setObjectName(QStringLiteral("fieldEdit"));
    idEdit->setPlaceholderText(QStringLiteral("18FF50E5"));
    idEdit->setFixedWidth(220);
    idEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,8}")), idEdit));
    addDialogRow(QStringLiteral("帧ID"), idEdit);

    auto *typeCombo = new QComboBox(&dialog);
    typeCombo->addItems({QStringLiteral("扩展帧"), QStringLiteral("标准帧")});
    configureComboBox(typeCombo);
    typeCombo->setFixedWidth(220);
    addDialogRow(QStringLiteral("帧类型"), typeCombo);

    auto *formatCombo = new QComboBox(&dialog);
    formatCombo->addItems({QStringLiteral("数据帧"), QStringLiteral("远程帧")});
    configureComboBox(formatCombo);
    formatCombo->setFixedWidth(220);
    addDialogRow(QStringLiteral("帧格式"), formatCombo);

    auto *dataEdit = new QLineEdit(&dialog);
    dataEdit->setObjectName(QStringLiteral("fieldEdit"));
    dataEdit->setPlaceholderText(QStringLiteral("11 22 33 44 55 66 77 88"));
    dataEdit->setFixedWidth(220);
    connect(dataEdit, &QLineEdit::textEdited, this, [dataEdit](const QString &text) {
        const QString formatted = formatFrameDataText(text);
        if (formatted == text) {
            return;
        }
        const QSignalBlocker blocker(dataEdit);
        dataEdit->setText(formatted);
        dataEdit->setCursorPosition(formatted.size());
    });
    addDialogRow(QStringLiteral("帧数据"), dataEdit);

    auto *countSpin = new QSpinBox(&dialog);
    countSpin->setRange(1, 100000);
    countSpin->setValue(1);
    countSpin->setFixedWidth(220);
    addDialogRow(QStringLiteral("发送次数"), countSpin);

    auto *intervalSpin = new QSpinBox(&dialog);
    intervalSpin->setRange(0, 600000);
    intervalSpin->setValue(1000);
    intervalSpin->setSuffix(QStringLiteral(" ms"));
    intervalSpin->setFixedWidth(220);
    addDialogRow(QStringLiteral("间隔"), intervalSpin);

    layout->addWidget(formPanel);
    layout->addStretch();

    auto *buttonRow = new QHBoxLayout();
    auto *confirm = makePrimaryButton(QStringLiteral("确认"), &dialog);
    confirm->setProperty("tone", QStringLiteral("accent"));
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    cancel->setObjectName(QStringLiteral("secondaryButton"));
    buttonRow->addStretch();
    buttonRow->addWidget(confirm);
    buttonRow->addWidget(cancel);
    layout->addLayout(buttonRow);

    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, [this, idEdit, dataEdit, typeCombo, formatCombo, countSpin, intervalSpin,
                                                    channelIndex, &dialog]() {
        confirmSendFrameDialog(idEdit, dataEdit, typeCombo, formatCombo, countSpin, intervalSpin, channelIndex, &dialog);
    });

    dialog.exec();
}

bool MainWindow::confirmSendFrameDialog(QLineEdit *idEdit, QLineEdit *dataEdit, QComboBox *typeCombo, QComboBox *formatCombo,
                                        QSpinBox *countSpin, QSpinBox *intervalSpin, int channelIndex, QDialog *dialog)
{
    const QString frameId = compactHexText(idEdit->text(), 8);
    if (frameId.isEmpty()) {
        QMessageBox::warning(dialog, QStringLiteral("提示"), QStringLiteral("请填写帧ID"));
        return false;
    }

    bool idOk = false;
    const uint idValue = frameId.toUInt(&idOk, 16);
    if (!idOk || idValue > 0x1FFFFFFFU) {
        QMessageBox::warning(dialog, QStringLiteral("提示"), QStringLiteral("帧ID超出范围"));
        return false;
    }
    if (typeCombo->currentText() == QStringLiteral("标准帧") && idValue > 0x7FFU) {
        QMessageBox::warning(dialog, QStringLiteral("提示"), QStringLiteral("标准帧ID不能超过7FF"));
        return false;
    }

    const QString compactData = compactHexText(dataEdit->text(), 16);
    if (compactData.size() % 2 != 0) {
        QMessageBox::warning(dialog, QStringLiteral("提示"), QStringLiteral("帧数据需要按完整字节输入，例如 01 02"));
        return false;
    }

    SendFrameConfig config;
    config.channelIndex = channelIndex;
    config.frameId = frameId;
    config.frameType = typeCombo->currentText();
    config.frameFormat = formatCombo->currentText();
    config.frameData = formatFrameDataText(dataEdit->text());
    config.dataLength = frameDataLength(config.frameData);
    config.sendCount = countSpin->value();
    config.intervalMs = intervalSpin->value();
    addSendFrameRow(config);
    dialog->accept();
    return true;
}

void MainWindow::loadSendFrames()
{
    QSettings settings;
    for (int channelIndex = 0; channelIndex <= 1; ++channelIndex) {
        const QString arrayName = QStringLiteral("sendFrames/can%1").arg(channelIndex);
        const int count = settings.beginReadArray(arrayName);
        for (int i = 0; i < count; ++i) {
            settings.setArrayIndex(i);

            SendFrameConfig config;
            config.channelIndex = channelIndex;
            config.frameId = settings.value(QStringLiteral("frameId")).toString();
            config.frameType = settings.value(QStringLiteral("frameType"), QStringLiteral("扩展帧")).toString();
            config.frameFormat = settings.value(QStringLiteral("frameFormat"), QStringLiteral("数据帧")).toString();
            config.frameData = settings.value(QStringLiteral("frameData")).toString();
            config.dataLength = settings.value(QStringLiteral("dataLength"), frameDataLength(config.frameData)).toInt();
            config.sendCount = settings.value(QStringLiteral("sendCount"), 1).toInt();
            config.intervalMs = settings.value(QStringLiteral("intervalMs"), 1000).toInt();

            if (!config.frameId.isEmpty()) {
                addSendFrameRow(config, false);
            }
        }
        settings.endArray();
        updateSendEmptyState(channelIndex);
    }
}

void MainWindow::saveSendFrames() const
{
    QSettings settings;
    settings.remove(QStringLiteral("sendFrames"));

    const auto writeChannel = [&settings](int channelIndex, const QVector<SendFrameConfig> &frames) {
        settings.beginWriteArray(QStringLiteral("sendFrames/can%1").arg(channelIndex));
        for (int i = 0; i < frames.size(); ++i) {
            const auto &config = frames.at(i);
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("frameId"), config.frameId);
            settings.setValue(QStringLiteral("frameType"), config.frameType);
            settings.setValue(QStringLiteral("frameFormat"), config.frameFormat);
            settings.setValue(QStringLiteral("frameData"), config.frameData);
            settings.setValue(QStringLiteral("dataLength"), config.dataLength);
            settings.setValue(QStringLiteral("sendCount"), config.sendCount);
            settings.setValue(QStringLiteral("intervalMs"), config.intervalMs);
        }
        settings.endArray();
    };

    writeChannel(0, can0SendFrames_);
    writeChannel(1, can1SendFrames_);
    settings.sync();
}

void MainWindow::removeStoredSendFrame(const SendFrameConfig &config)
{
    auto &frames = config.channelIndex == 0 ? can0SendFrames_ : can1SendFrames_;
    for (int i = 0; i < frames.size(); ++i) {
        const auto &candidate = frames.at(i);
        const bool sameFrame = candidate.channelIndex == config.channelIndex && candidate.frameId == config.frameId
            && candidate.frameType == config.frameType && candidate.frameFormat == config.frameFormat
            && candidate.frameData == config.frameData && candidate.dataLength == config.dataLength
            && candidate.sendCount == config.sendCount && candidate.intervalMs == config.intervalMs;
        if (sameFrame) {
            frames.removeAt(i);
            break;
        }
    }

    updateSendEmptyState(config.channelIndex);
    saveSendFrames();
}

void MainWindow::updateSendEmptyState(int channelIndex)
{
    auto *emptyLabel = channelIndex == 0 ? can0SendEmptyLabel_ : can1SendEmptyLabel_;
    const auto &frames = channelIndex == 0 ? can0SendFrames_ : can1SendFrames_;
    if (emptyLabel != nullptr) {
        emptyLabel->setVisible(frames.isEmpty());
    }
}

void MainWindow::addSendFrameRow(const SendFrameConfig &config, bool persist)
{
    auto *listLayout = config.channelIndex == 0 ? can0SendListLayout_ : can1SendListLayout_;
    auto *emptyLabel = config.channelIndex == 0 ? can0SendEmptyLabel_ : can1SendEmptyLabel_;
    auto &frames = config.channelIndex == 0 ? can0SendFrames_ : can1SendFrames_;
    if (!listLayout || !emptyLabel) {
        return;
    }

    frames.append(config);
    emptyLabel->setVisible(false);
    if (persist) {
        saveSendFrames();
    }

    auto *parentWidget = qobject_cast<QWidget *>(listLayout->parent());
    auto *row = new QWidget(parentWidget);
    row->setObjectName(QStringLiteral("sendFrameRow"));
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(22, 15, 18, 15);
    rowLayout->setSpacing(18);

    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(4);
    auto *idLabel = new QLabel(QStringLiteral("ID  %1").arg(config.frameId), row);
    idLabel->setObjectName(QStringLiteral("sendFrameId"));
    auto *metaLabel = new QLabel(QStringLiteral("%1 · %2 · DLC %3 · %4次 · %5 ms")
                                     .arg(config.frameType)
                                     .arg(config.frameFormat)
                                     .arg(config.dataLength)
                                     .arg(config.sendCount)
                                     .arg(config.intervalMs),
                                 row);
    metaLabel->setObjectName(QStringLiteral("sendFrameMeta"));
    const QString dataText = config.frameData.isEmpty() ? QStringLiteral("无数据") : config.frameData;
    auto *dataLabel = new QLabel(QStringLiteral("数据  %1").arg(dataText), row);
    dataLabel->setObjectName(QStringLiteral("sendFrameData"));
    textColumn->addWidget(idLabel);
    textColumn->addWidget(metaLabel);
    textColumn->addWidget(dataLabel);

    auto *sendButton = makePrimaryButton(QStringLiteral("发送"), row);
    sendButton->setProperty("tone", QStringLiteral("secondary"));
    auto *deleteButton = new QPushButton(QStringLiteral("删除"), row);
    deleteButton->setObjectName(QStringLiteral("deleteButton"));
    deleteButton->setCursor(Qt::PointingHandCursor);
    deleteButton->setMinimumHeight(38);
    auto activeTimer = std::make_shared<QPointer<QTimer>>();
    connect(sendButton, &QPushButton::clicked, this, [this, config, sendButton, activeTimer]() {
        if (*activeTimer) {
            (*activeTimer)->stop();
            (*activeTimer)->deleteLater();
            *activeTimer = nullptr;
            setSendButtonSending(sendButton, false);
            messageLabel_->setText(QStringLiteral("CAN%1发送已停止").arg(config.channelIndex));
            return;
        }

        const QByteArray data = frameDataBytes(config.frameData);
        const bool extendedFrame = config.frameType == QStringLiteral("扩展帧");
        const bool remoteFrame = config.frameFormat == QStringLiteral("远程帧");
        auto sendOnce = [this, config, data, extendedFrame, remoteFrame]() {
            return controller_->transmitFrame(config.channelIndex, config.frameId, extendedFrame, remoteFrame, data);
        };

        setSendButtonSending(sendButton, true);
        if (config.sendCount <= 1) {
            sendOnce();
            setSendButtonSending(sendButton, false);
            return;
        }

        auto remaining = std::make_shared<int>(config.sendCount);
        auto *timer = new QTimer(this);
        if (!sendOnce()) {
            timer->deleteLater();
            setSendButtonSending(sendButton, false);
            return;
        }
        --(*remaining);
        if (*remaining <= 0) {
            timer->deleteLater();
            setSendButtonSending(sendButton, false);
            return;
        }
        *activeTimer = timer;
        connect(timer, &QObject::destroyed, this, [sendButton, activeTimer]() {
            *activeTimer = nullptr;
            setSendButtonSending(sendButton, false);
        });
        connect(timer, &QTimer::timeout, this, [timer, remaining, sendOnce]() mutable {
            if (*remaining <= 0) {
                timer->stop();
                timer->deleteLater();
                return;
            }
            if (!sendOnce()) {
                timer->stop();
                timer->deleteLater();
                return;
            }
            --(*remaining);
            if (*remaining <= 0) {
                timer->stop();
                timer->deleteLater();
            }
        });
        timer->setInterval(qMax(0, config.intervalMs));
        timer->start();
    });
    connect(deleteButton, &QPushButton::clicked, this, [this, row, config, activeTimer]() {
        if (*activeTimer) {
            (*activeTimer)->stop();
            (*activeTimer)->deleteLater();
            *activeTimer = nullptr;
        }
        removeStoredSendFrame(config);
        row->deleteLater();
        messageLabel_->setText(QStringLiteral("CAN%1发送帧已删除").arg(config.channelIndex));
    });

    rowLayout->addLayout(textColumn, 1);
    rowLayout->addWidget(sendButton);
    rowLayout->addWidget(deleteButton);
    listLayout->insertWidget(qMax(0, listLayout->count() - 1), row);
}

void MainWindow::refreshState(const DeviceUiState &state)
{
    if (deviceTypeCombo_->currentText() != state.deviceType) {
        deviceTypeCombo_->setCurrentText(state.deviceType);
    }
    if (deviceIndexSpin_->value() != state.deviceIndex) {
        deviceIndexSpin_->setValue(state.deviceIndex);
    }

    deviceStatusPill_->setText(state.deviceOpen ? QStringLiteral("已打开") : QStringLiteral("未打开"));
    deviceStatusPill_->setProperty("state", state.deviceOpen ? QStringLiteral("ok") : QStringLiteral("idle"));
    deviceStatusPill_->style()->unpolish(deviceStatusPill_);
    deviceStatusPill_->style()->polish(deviceStatusPill_);

    deviceNameLabel_->setText(QStringLiteral("%1  设备%2").arg(state.deviceType).arg(state.deviceIndex));
    deviceTreePanel_->setVisible(state.deviceOpen);
    openDeviceButton_->setEnabled(true);
    openDeviceButton_->setText(state.deviceOpen ? QStringLiteral("关闭设备") : QStringLiteral("打开设备"));
    deviceTypeCombo_->setEnabled(!state.deviceOpen);
    deviceIndexSpin_->setEnabled(!state.deviceOpen);
    openDeviceButton_->setProperty("tone", state.deviceOpen ? QStringLiteral("destructive") : QStringLiteral("accent"));
    openDeviceButton_->setProperty("active", state.deviceOpen);
    repolish(openDeviceButton_);

    channel0Status_->setText(state.channel0Started ? state.bitrate : QStringLiteral("未启动"));
    channel0Status_->setProperty("state", state.channel0Started ? QStringLiteral("ok") : QStringLiteral("idle"));
    channel1Status_->setText(state.channel1Started ? state.bitrate : QStringLiteral("未启动"));
    channel1Status_->setProperty("state", state.channel1Started ? QStringLiteral("ok") : QStringLiteral("idle"));
    channel0StartButton_->setText(state.channel0Started ? QStringLiteral("重新启动") : QStringLiteral("启动"));
    channel1StartButton_->setText(state.channel1Started ? QStringLiteral("重新启动") : QStringLiteral("启动"));
    channel0StartButton_->setProperty("active", state.channel0Started);
    channel1StartButton_->setProperty("active", state.channel1Started);

    for (auto *label : {channel0Status_, channel1Status_}) {
        repolish(label);
    }
    for (auto *button : {channel0StartButton_, channel1StartButton_}) {
        repolish(button);
    }
}

void MainWindow::showStartDialog(int channelIndex)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("startDialog"));
    dialog.setWindowTitle(QStringLiteral("启动"));
    dialog.setModal(true);
    dialog.resize(460, 310);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(30, 28, 30, 26);
    layout->setSpacing(20);

    auto *title = new QLabel(QStringLiteral("通道%1 启动设置").arg(channelIndex), &dialog);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);

    auto *bitrateRow = new QHBoxLayout();
    auto *bitrateLabel = new QLabel(QStringLiteral("波特率"), &dialog);
    auto *bitrateCombo = new QComboBox(&dialog);
    bitrateCombo->addItems({QStringLiteral("125kbps"), QStringLiteral("250kbps"), QStringLiteral("500kbps"), QStringLiteral("1Mbps")});
    configureComboBox(bitrateCombo);
    bitrateCombo->setCurrentText(controller_->state().bitrate);
    bitrateRow->addWidget(bitrateLabel);
    bitrateRow->addStretch();
    bitrateRow->addWidget(bitrateCombo);
    layout->addLayout(bitrateRow);

    auto *modeRow = new QHBoxLayout();
    auto *modeLabel = new QLabel(QStringLiteral("工作模式"), &dialog);
    auto *modeCombo = new QComboBox(&dialog);
    modeCombo->addItems({QStringLiteral("正常模式"), QStringLiteral("只听模式")});
    configureComboBox(modeCombo);
    modeCombo->setCurrentText(controller_->state().workMode);
    modeRow->addWidget(modeLabel);
    modeRow->addStretch();
    modeRow->addWidget(modeCombo);
    layout->addLayout(modeRow);
    layout->addStretch();

    auto *buttonRow = new QHBoxLayout();
    auto *confirm = makePrimaryButton(QStringLiteral("确认"), &dialog);
    confirm->setProperty("tone", QStringLiteral("accent"));
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    cancel->setObjectName(QStringLiteral("secondaryButton"));
    buttonRow->addStretch();
    buttonRow->addWidget(confirm);
    buttonRow->addWidget(cancel);
    layout->addLayout(buttonRow);

    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, &dialog, [&]() {
        if (controller_->startChannel(channelIndex, bitrateCombo->currentText(), modeCombo->currentText())) {
            dialog.accept();
        }
    });

    dialog.exec();
}

QFrame *MainWindow::makeCardPanel(QWidget *parent) const
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("cardPanel"));
    return panel;
}

QPushButton *MainWindow::makePrimaryButton(const QString &text, QWidget *parent) const
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("primaryButton"));
    button->setMinimumHeight(44);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("active", false);
    return button;
}

QPushButton *MainWindow::makeMenuButton(const QString &text, const QString &symbol, QWidget *parent) const
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("menuButton"));
    button->setCheckable(true);
    button->setMinimumHeight(52);
    button->setCursor(Qt::PointingHandCursor);
    if (symbol == QStringLiteral("D")) {
        button->setIcon(QIcon(QStringLiteral(":/icons/device.svg")));
    } else if (symbol == QStringLiteral("TX")) {
        button->setIcon(QIcon(QStringLiteral(":/icons/can-send.svg")));
    } else if (symbol == QStringLiteral("RX")) {
        button->setIcon(QIcon(QStringLiteral(":/icons/can-receive.svg")));
    }
    button->setIconSize(QSize(22, 22));
    return button;
}

QLabel *MainWindow::makeStatusPill(const QString &text, QWidget *parent) const
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("statusPill"));
    label->setProperty("state", QStringLiteral("idle"));
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumWidth(80);
    label->setMinimumHeight(30);
    return label;
}

void MainWindow::applyAppStyle()
{
    setStyleSheet(QStringLiteral(R"(
        * {
            outline: none;
            font-family: "Microsoft YaHei UI", "Segoe UI";
        }
        QMainWindow {
            font-size: 17px;
            color: #1c1c1e;
        }
        #appRoot {
            background: #f2f2f7;
        }
        #sidebar, #connectPanel, #deviceTreePanel, #sendListPanel, #receivePanel, #dialogFormPanel {
            background: #ffffff;
            border: 1px solid #e5e5ea;
            border-radius: 18px;
        }
        #contentStack, #contentPage {
            background: transparent;
        }
        #pageTitle {
            font-size: 34px;
            font-weight: 700;
            letter-spacing: 0px;
            color: #1c1c1e;
        }
        #menuButton {
            text-align: left;
            padding: 0 18px 0 16px;
            min-height: 52px;
            border: 0px;
            border-radius: 12px;
            background: transparent;
            color: #1c1c1e;
            font-weight: 600;
            font-size: 17px;
        }
        #menuButton:hover {
            background: #f2f2f7;
        }
        #menuButton:checked {
            background: #e8f1ff;
            color: #007aff;
        }
        #menuButton:pressed {
            background: #dbeaff;
        }
        #menuButton:disabled {
            color: #8e8e93;
        }
        #primaryButton {
            min-width: 108px;
            min-height: 38px;
            border: 0px;
            border-radius: 10px;
            background: #007aff;
            color: white;
            font-weight: 600;
            font-size: 16px;
            padding: 6px 18px;
        }
        #primaryButton:hover {
            background: #0a84ff;
        }
        #primaryButton:pressed {
            background: #0066d6;
        }
        #primaryButton[tone="secondary"] {
            color: #007aff;
            background: #eef5ff;
        }
        #primaryButton[tone="secondary"]:hover {
            background: #e2efff;
        }
        #primaryButton[tone="secondary"]:pressed {
            background: #d4e7ff;
        }
        #primaryButton[tone="destructive"] {
            color: white;
            background: #ff3b30;
        }
        #primaryButton[active="true"] {
            background: #34c759;
            color: white;
        }
        #primaryButton[tone="secondary"][active="true"] {
            color: #0a7f33;
            background: #e8f8ee;
        }
        #primaryButton[tone="destructive"][active="true"] {
            color: white;
            background: #ff3b30;
        }
        #primaryButton:disabled {
            background: #d1d1d6;
            color: #ffffff;
        }
        #addButton {
            min-width: 46px;
            min-height: 46px;
            max-width: 46px;
            max-height: 46px;
            border: 0px;
            border-radius: 23px;
            background: #007aff;
            color: white;
            font-size: 28px;
            font-weight: 500;
            padding: 0px;
        }
        #addButton:hover {
            background: #0a84ff;
        }
        #addButton:pressed {
            background: #0066d6;
        }
        #secondaryButton {
            min-width: 108px;
            min-height: 38px;
            border: 0px;
            border-radius: 10px;
            background: #e5e5ea;
            color: #1c1c1e;
            font-weight: 600;
            font-size: 16px;
            padding: 6px 18px;
        }
        #secondaryButton:hover {
            background: #d8d8de;
        }
        #secondaryButton:pressed {
            background: #c7c7cc;
        }
        #deleteButton {
            min-width: 76px;
            min-height: 38px;
            border: 0px;
            border-radius: 10px;
            background: #fff1f0;
            color: #ff3b30;
            font-weight: 700;
            font-size: 16px;
            padding: 6px 14px;
        }
        #deleteButton:hover {
            background: #ffe4e1;
        }
        #deleteButton:pressed {
            background: #ffd4cf;
        }
        QComboBox {
            min-height: 36px;
            border: 0px;
            border-radius: 0px;
            background: transparent;
            padding: 3px 34px 3px 10px;
            selection-background-color: #007aff;
            color: #3a3a3c;
            font-size: 16px;
            font-weight: 500;
        }
        QComboBox:focus {
            background: transparent;
            color: #1c1c1e;
        }
        QComboBox:disabled {
            color: #8e8e93;
            background: transparent;
        }
        QSpinBox {
            min-height: 36px;
            border: 1px solid #d1d1d6;
            border-radius: 10px;
            background: #f9f9fb;
            padding: 3px 38px 3px 12px;
            selection-background-color: #007aff;
            color: #1c1c1e;
            font-size: 16px;
            font-weight: 500;
        }
        QSpinBox:focus {
            border: 1px solid #007aff;
            background: #ffffff;
        }
        QSpinBox:disabled {
            color: #8e8e93;
            background: #f2f2f7;
        }
        QLineEdit#fieldEdit {
            min-height: 36px;
            border: 1px solid #d1d1d6;
            border-radius: 10px;
            background: #f9f9fb;
            padding: 3px 12px;
            selection-background-color: #007aff;
            color: #1c1c1e;
            font-size: 16px;
            font-weight: 500;
        }
        QLineEdit#fieldEdit:focus {
            border: 1px solid #007aff;
            background: #ffffff;
        }
        QLineEdit#filterEdit {
            min-height: 36px;
            border: 1px solid #d1d1d6;
            border-radius: 10px;
            background: #f9f9fb;
            padding: 3px 12px;
            selection-background-color: #007aff;
            color: #1c1c1e;
            font-size: 16px;
            font-weight: 500;
        }
        QLineEdit#filterEdit:focus {
            border: 1px solid #007aff;
            background: #ffffff;
        }
        QComboBox::drop-down {
            width: 34px;
            border: 0px;
            background: transparent;
        }
        QComboBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 14px;
            height: 14px;
        }
        QSpinBox::up-button {
            subcontrol-origin: border;
            subcontrol-position: top right;
            width: 34px;
            height: 18px;
            border: 0px;
            border-top-right-radius: 10px;
            background: transparent;
        }
        QSpinBox::down-button {
            subcontrol-origin: border;
            subcontrol-position: bottom right;
            width: 34px;
            height: 18px;
            border: 0px;
            border-bottom-right-radius: 10px;
            background: transparent;
        }
        QSpinBox::up-arrow {
            image: url(:/icons/chevron-up.svg);
            width: 12px;
            height: 12px;
        }
        QSpinBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 12px;
            height: 12px;
        }
        #settingRow {
            background: transparent;
            border-bottom: 1px solid #e5e5ea;
        }
        #fieldLabel, #treeDeviceLabel, #treeChannelLabel {
            font-weight: 600;
            color: #1c1c1e;
            font-size: 17px;
        }
        #treeDeviceLabel {
            font-size: 18px;
            color: #1c1c1e;
        }
        #treeChannelLabel {
            min-height: 38px;
            font-size: 17px;
        }
        #statusPill {
            border-radius: 12px;
            border: 0px;
            background: #e5e5ea;
            color: #636366;
            font-size: 15px;
            font-weight: 600;
            padding: 3px 10px;
        }
        #statusPill[state="ok"] {
            background: #dff7e8;
            color: #188038;
        }
        #messageLabel {
            color: #636366;
            font-size: 15px;
            padding: 10px 12px;
            border-radius: 12px;
            background: #f2f2f7;
        }
        #sendScrollArea, #sendListContent {
            border: 0px;
            background: transparent;
        }
        #receiveToolbar {
            background: transparent;
        }
        QTableWidget#receiveTable {
            border: 1px solid #e5e5ea;
            border-radius: 12px;
            background: #ffffff;
            alternate-background-color: #fafafa;
            color: #1c1c1e;
            gridline-color: #ededf2;
            font-size: 15px;
            selection-background-color: #e8f1ff;
            selection-color: #1c1c1e;
        }
        QTableWidget#receiveTable::item {
            padding: 8px 10px;
            border: 0px;
        }
        QHeaderView::section {
            background: #f2f2f7;
            color: #636366;
            border: 0px;
            border-bottom: 1px solid #e5e5ea;
            padding: 9px 10px;
            font-size: 14px;
            font-weight: 700;
        }
        #sendFrameRow {
            background: transparent;
            border-bottom: 1px solid #e5e5ea;
        }
        #sendFrameId {
            color: #1c1c1e;
            font-size: 18px;
            font-weight: 700;
        }
        #sendFrameMeta {
            color: #636366;
            font-size: 14px;
            font-weight: 500;
        }
        #sendFrameData {
            color: #2c2c2e;
            font-size: 16px;
            font-weight: 600;
        }
        #emptyStateLabel {
            color: #8e8e93;
            font-size: 17px;
            font-weight: 600;
        }
        #dialogTitle {
            font-size: 24px;
            font-weight: 700;
            color: #1c1c1e;
        }
        QDialog {
            background: #f2f2f7;
            color: #1c1c1e;
        }
        QDialog QLabel {
            color: #1c1c1e;
            font-weight: 600;
            font-size: 18px;
        }
        QComboBox QAbstractItemView {
            border: 1px solid #d1d1d6;
            border-radius: 0px;
            background: #ffffff;
            color: #1c1c1e;
            selection-background-color: #007aff;
            selection-color: white;
            padding: 0px;
            margin: 0px;
            outline: 0px;
        }
    )"));
}
