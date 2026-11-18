#include "main_plugin.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickWindow>

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

void loadUiFont(const QString& appDir)
{
    const QDir fonts(appDir + QStringLiteral("/fonts"));
    const auto names = fonts.entryList({QStringLiteral("AlibabaPuHuiTi*Regular*.ttf")}, QDir::Files);
    if (!names.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(fonts.filePath(names.first()));
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty())
            QGuiApplication::setFont(QFont(families.first()));
    }
}

void qtMessage(QtMsgType, const QMessageLogContext&, const QString& message)
{
    const QByteArray line = message.toUtf8() + '\n';
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), line.constData(),
        static_cast<DWORD>(line.size()), &written, nullptr);
}

} // namespace

extern "C" __declspec(dllexport) int winshunCursorWrapMain(const wchar_t* appDirName)
{
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    PROCESS_POWER_THROTTLING_STATE throttling {PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0};
    ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof throttling);

    winshun::detail::LineReader input(::GetStdHandle(STD_INPUT_HANDLE));
    std::string first;
    if (!input.next(first))
        return 2;
    const auto initialize = winshun::Json::parse(first);
    if (!initialize || (*initialize)["method"].toString() != "initialize") {
        std::fputs("cursorwrap: expected initialize on stdin\n", stderr);
        return 2;
    }

    const QString appDir = QString::fromWCharArray(appDirName);
    const bool software = (*initialize)["params"]["appearance"]["renderer"].toString() == "software";
    qInstallMessageHandler(qtMessage);
    if (QFileInfo::exists(appDir + QStringLiteral("/platforms/qwindows.dll")))
        QCoreApplication::setLibraryPaths({appDir});
    if (qEnvironmentVariableIsEmpty("QML_IMPORT_PATH")) {
        const QDir layout(appDir);
        const QString imports = layout.exists(QStringLiteral("qml"))
            ? layout.absoluteFilePath(QStringLiteral("qml"))
            : layout.absoluteFilePath(QStringLiteral("../qml"));
        qputenv("QML_IMPORT_PATH", QDir::toNativeSeparators(imports).toUtf8());
    }
    qputenv("QT_QPA_PLATFORM", software ? "windows:fontengine=gdi" : "windows");
    QQuickWindow::setGraphicsApi(software ? QSGRendererInterface::Software : QSGRendererInterface::Direct3D11);
    int argc = 1;
    char name[] = "cursorwrap";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    loadUiFont(appDir);
    const auto plugin = cw::makePlugin();
    qInfo("CursorWrap Qt %s", qVersion());
    plugin->receiveLine(first);

    std::atomic<bool> stopping {false};
    std::thread reader([&] {
        std::string line;
        while (!stopping.load() && input.next(line)) {
            QMetaObject::invokeMethod(&app, [&, line = std::move(line)] {
                if (!stopping.load() && plugin->receiveLine(line)) {
                    stopping.store(true);
                    app.quit();
                }
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });

    const int result = app.exec();
    stopping.store(true);
    // Shutdown can arrive before the host closes stdin. Cancel the blocked
    // read, including one that entered ReadFile just as shutdown began.
    const HANDLE thread = reader.native_handle();
    do {
        ::CancelSynchronousIo(thread);
    } while (::WaitForSingleObject(thread, 10) == WAIT_TIMEOUT);
    reader.join();
    return result;
}
