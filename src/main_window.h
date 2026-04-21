#pragma once

#include "device_controller.h"

#include <QMainWindow>
#include <QVector>

class AiCommandBridge;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QVBoxLayout;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(DeviceController *controller, AiCommandBridge *aiBridge, QWidget *parent = nullptr);

private Q_SLOTS:
    void refreshState(const DeviceUiState &state);
    void showStartDialog(int channelIndex);

private:
    struct SendFrameConfig
    {
        int channelIndex = 0;
        QString frameId;
        QString frameType;
        QString frameFormat;
        QString frameData;
        int dataLength = 0;
        int sendCount = 1;
        int intervalMs = 1000;
    };

    void buildUi();
    void buildSidebar();
    void buildDevicePage();
    void buildCanSendPage(int channelIndex);
    void buildCanReceivePage(int channelIndex);
    void applyAppStyle();
    void selectPage(int pageIndex, QPushButton *selectedButton);
    void showAddSendFrameDialog(int channelIndex);
    void addSendFrameRow(const SendFrameConfig &config, bool persist = true);
    void loadSendFrames();
    void saveSendFrames() const;
    void removeStoredSendFrame(const SendFrameConfig &config);
    void updateSendEmptyState(int channelIndex);
    bool confirmSendFrameDialog(QLineEdit *idEdit, QLineEdit *dataEdit, QComboBox *typeCombo, QComboBox *formatCombo,
                                QSpinBox *countSpin, QSpinBox *intervalSpin, int channelIndex, QDialog *dialog);
    void pollReceiveFrames(int channelIndex);
    void refreshReceiveTable(int channelIndex);
    void clearReceiveFrames(int channelIndex);
    void stopReceiveTimers();

    QFrame *makeCardPanel(QWidget *parent = nullptr) const;
    QPushButton *makePrimaryButton(const QString &text, QWidget *parent = nullptr) const;
    QPushButton *makeMenuButton(const QString &text, const QString &symbol, QWidget *parent = nullptr) const;
    QLabel *makeStatusPill(const QString &text, QWidget *parent = nullptr) const;

    DeviceController *controller_ = nullptr;
    AiCommandBridge *aiBridge_ = nullptr;

    QStackedWidget *stack_ = nullptr;
    QVBoxLayout *menuLayout_ = nullptr;
    QVector<QPushButton *> menuButtons_;
    QComboBox *deviceTypeCombo_ = nullptr;
    QSpinBox *deviceIndexSpin_ = nullptr;
    QPushButton *openDeviceButton_ = nullptr;
    QLabel *deviceStatusPill_ = nullptr;
    QFrame *deviceTreePanel_ = nullptr;
    QLabel *deviceNameLabel_ = nullptr;
    QPushButton *channel0StartButton_ = nullptr;
    QPushButton *channel1StartButton_ = nullptr;
    QLabel *channel0Status_ = nullptr;
    QLabel *channel1Status_ = nullptr;
    QLabel *messageLabel_ = nullptr;
    QVBoxLayout *can0SendListLayout_ = nullptr;
    QVBoxLayout *can1SendListLayout_ = nullptr;
    QLabel *can0SendEmptyLabel_ = nullptr;
    QLabel *can1SendEmptyLabel_ = nullptr;
    QVector<SendFrameConfig> can0SendFrames_;
    QVector<SendFrameConfig> can1SendFrames_;
    QTableWidget *can0ReceiveTable_ = nullptr;
    QTableWidget *can1ReceiveTable_ = nullptr;
    QLineEdit *can0ReceiveFilterEdit_ = nullptr;
    QLineEdit *can1ReceiveFilterEdit_ = nullptr;
    QTimer *can0ReceiveTimer_ = nullptr;
    QTimer *can1ReceiveTimer_ = nullptr;
    QVector<CanRxFrame> can0ReceiveFrames_;
    QVector<CanRxFrame> can1ReceiveFrames_;
};
