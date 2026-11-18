#include "tuning_window_qt.h"

#include "Phosphor.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

namespace {

// Visual evidence for the Windows run: CW_TUNING_SCREENSHOTS names the folder.
void saveScreenshot(QQuickWindow* window, const char* name)
{
    const QString folder = qEnvironmentVariable("CW_TUNING_SCREENSHOTS");
    if (folder.isEmpty())
        return;
    window->grabWindow().save(QDir(folder).filePath(QString::fromLatin1(name)));
}

} // namespace

class TuningWindowTest : public QObject {
    Q_OBJECT

private slots:
    void editsAndReopens();
};

void TuningWindowTest::editsAndReopens()
{
    cw::SettingsUiState input;
    input.chinese = false;
    input.summary = "Host card: both directions";
    int commits = 0;
    cw::TuningWindowQml* controller = nullptr;
    cw::TuningWindowQml tuning([&] { return input; }, [&](const cw::RawSettings& raw) {
        cw::applySettings(raw, input.settings, true);
        ++commits;
        controller->settingsUpdated();
    });
    controller = &tuning;
    tuning.show();
    QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(QGuiApplication::topLevelWindows().first());
    QVERIFY(window);
    QTRY_VERIFY(window->isVisible());
    QCOMPARE(window->title(), QStringLiteral("Cursor wrap tuning"));

    auto* theme = qmlEngine(window)->singletonInstance<QObject*>(
        QStringLiteral("WinShun"), QStringLiteral("Theme"));
    QVERIFY(theme);
    QVERIFY(theme->property("cardRadius").toReal() > 0);
    auto* inputField = qvariant_cast<QQmlComponent*>(theme->property("inputField"));
    QVERIFY(inputField);
    QVERIFY2(inputField->isReady(), qPrintable(inputField->errorString()));
    QTest::qWait(150);
    saveScreenshot(window, "tuning-light.png");

    auto* root = window;
    auto* speed = root->findChild<QQuickItem*>("impactSpeedPpsInput");
    auto* apply = root->findChild<QQuickItem*>("impactSpeedPpsApply");
    auto* accel = root->findChild<QQuickItem*>("impactAccelPps2Input");
    auto* off = root->findChild<QQuickItem*>("accelerationOff");
    auto* glow = root->findChild<QQuickItem*>("impactFlash");
    auto* recommendation = root->findChild<QQuickItem*>("adoptAdvice");
    auto* reset = root->findChild<QQuickItem*>("resetTuning");
    QVERIFY(speed && apply && accel && off && glow && recommendation && reset);
    QVERIFY(!accel->isEnabled());
    QVERIFY(!recommendation->isEnabled());
    QCOMPARE(speed->property("text").toString(), QStringLiteral("2000"));

    speed->setProperty("text", QStringLiteral("3500"));
    QCOMPARE(commits, 0);
    QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
    QCOMPARE(input.settings.impactSpeedPps, 3500);
    QCOMPARE(commits, 1);

    speed->setProperty("text", QStringLiteral("999999"));
    QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
    QCOMPARE(input.settings.impactSpeedPps, 3500);
    QCOMPARE(commits, 1);
    QVERIFY(root->findChild<QQuickItem*>("impactSpeedPpsError")->isVisible());

    off->setProperty("checked", false);
    QVERIFY(QMetaObject::invokeMethod(off, "toggled"));
    QVERIFY(accel->isEnabled());
    QCOMPARE(input.settings.impactAccelPps2, 0);
    accel->setProperty("text", QStringLiteral("2000"));
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QQuickItem*>("impactAccelPps2Apply"), "clicked"));
    QCOMPARE(input.settings.impactAccelPps2, 2000);
    off->setProperty("checked", true);
    QVERIFY(QMetaObject::invokeMethod(off, "toggled"));
    QCOMPARE(input.settings.impactAccelPps2, 0);
    QVERIFY(!accel->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(glow, "toggled", Q_ARG(bool, true)));
    QVERIFY(input.settings.impactFlash);
    input.advice.pps = 4100;
    input.arrivals = 24;
    tuning.settingsUpdated();
    QVERIFY(recommendation->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(recommendation, "clicked"));
    QCOMPARE(input.settings.impactSpeedPps, 4100);
    QVERIFY(QMetaObject::invokeMethod(reset, "clicked"));
    QCOMPARE(input.settings.impactSpeedPps, cw::Settings{}.impactSpeedPps);
    QVERIFY(input.settings.impactFlash);

    tuning.applyTheme(QStringLiteral("dark"), {});
    QCOMPARE(window->color(), QColor("#202020"));
    input.chinese = true;
    tuning.settingsUpdated();
    QTest::qWait(150);
    saveScreenshot(window, "tuning-dark.png");
    QCOMPARE(window->title(), QStringLiteral("回绕调参"));
    QVERIFY(window->close());
    tuning.show();
    QVERIFY(window->isVisible());
    QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
    tuning.shutdown();
}

int main(int argc, char** argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    // As the plugin's own startup does: the window's title bar draws a
    // Phosphor glyph, and Glyph.qml draws nothing for an unknown name.
    ws::phosphor::loadFonts();
    TuningWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_tuning_window.moc"
