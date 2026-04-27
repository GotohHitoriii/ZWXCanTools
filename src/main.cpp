#include "ai_command_bridge.h"
#include "device_controller.h"
#include "main_window.h"

#include <QApplication>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ZWXCanTools"));
    QApplication::setOrganizationName(QStringLiteral("ZWX"));

    auto controller = std::make_unique<DeviceController>();
    auto aiBridge = std::make_unique<AiCommandBridge>(controller.get());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, aiBridge.get(), [bridge = aiBridge.get()]() {
        bridge->stopLocalServer();
    });

    auto window = std::make_unique<MainWindow>(controller.get(), aiBridge.get());
    window->resize(1360, 820);
    window->show();
    QTimer::singleShot(0, aiBridge.get(), [bridge = aiBridge.get()]() {
        bridge->startLocalServer();
    });

    const int exitCode = app.exec();
    window.reset();
    aiBridge.reset();
    controller.reset();
    return exitCode;
}
