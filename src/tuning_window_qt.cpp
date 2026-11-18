#include "tuning_window_qt.h"

#include "SystemTheme.h"
#include "WindowFrame.h"
#include "platform/WindowEffects.h"

#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStyleHints>

#include <utility>

namespace cw {

TuningWindowQml::TuningWindowQml(Provider provider, Apply apply)
    : m_provider(std::move(provider)), m_apply(std::move(apply))
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &TuningWindowQml::settingsUpdated);
}

TuningWindowQml::~TuningWindowQml()
{
    shutdown();
}

void TuningWindowQml::show()
{
    settingsUpdated();
    if (!m_engine) {
        m_frame = std::make_unique<ws::WindowFrame>(true);
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({
            {QStringLiteral("tuning"), QVariant::fromValue(this)},
            {QStringLiteral("look"), QVariant::fromValue(&m_theme)},
            {QStringLiteral("windowFrame"), QVariant::fromValue(m_frame.get())}
        });
        m_engine->loadFromModule(QStringLiteral("WinShun"), QStringLiteral("TuningWindow"));
        if (!m_engine->rootObjects().isEmpty())
            m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
        if (!m_window) {
            m_engine.reset();
            m_frame.reset();
            return;
        }
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            m_frame->setWindow(m_window);
            updateFrame();
        }
        connect(m_window, &QWindow::visibleChanged, this, [this](bool visible) {
            if (visible) {
                updateFrame();
                m_timer.start();
            } else {
                m_timer.stop();
            }
        });
    }
    if (m_window->visibility() == QWindow::Minimized)
        m_window->showNormal();
    else
        m_window->show();
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        m_window->raise();
        m_window->requestActivate();
    }
}

void TuningWindowQml::shutdown()
{
    m_timer.stop();
    m_engine.reset();
    m_frame.reset();
    m_window = nullptr;
}

void TuningWindowQml::applyTheme(const QString& theme, const QVariantMap& appearance)
{
    m_theme.apply(theme, appearance);
    QGuiApplication::styleHints()->setColorScheme(m_theme.dark()
        ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    ws::SystemTheme::setRoundedCorners(m_theme.rounded());
    ws::SystemTheme::setBackdropAvailable(QGuiApplication::platformName() == QStringLiteral("windows"));
    ws::SystemTheme::setBackdropOn(m_theme.backdropOn());
    updateFrame();
}

void TuningWindowQml::updateFrame()
{
    if (!m_window)
        return;
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
        return;
    // The host's own frameless look: rounded corners, the DWM shadow and border,
    // and Mica behind the content. Its frame extension stays at the window's
    // border; extended over the whole window, DWM would draw its own caption
    // buttons over TitleBar.qml's.
    ws::win::styleFramelessWindow(m_window, m_theme.backdropOn());
    const bool backdrop = m_theme.backdropOn() && ws::win::backdropSupported()
        && ws::win::materialsEnabled();
    if (m_backdrop != backdrop) {
        m_backdrop = backdrop;
        emit backdropChanged();
    }
}

void TuningWindowQml::settingsUpdated()
{
    const SettingsUiState s = m_provider();
    QVariantMap next {
        {QStringLiteral("summary"), QString::fromStdString(s.summary)},
        {QStringLiteral("chinese"), s.chinese},
        {QStringLiteral("adviceFits"), s.advice.fits},
        {QStringLiteral("advicePps"), s.advice.pps},
        {QStringLiteral("arrivals"), static_cast<qlonglong>(s.arrivals)},
    };
    for (const auto& field : settingsFields()) {
        if (field.owner != FieldOwner::Plugin)
            continue;
        const auto key = QString::fromLatin1(field.key);
        if (field.kind == FieldKind::Number) {
            SettingRange range;
            if (settingRange(field.key, range)) {
                next.insert(key, QVariantMap {
                    {QStringLiteral("value"), static_cast<qlonglong>(field.number(s.settings))},
                    {QStringLiteral("minimum"), static_cast<qlonglong>(range.lo)},
                    {QStringLiteral("maximum"), static_cast<qlonglong>(range.hi)},
                });
            }
        } else if (field.kind == FieldKind::Bool) {
            next.insert(key, field.boolean(s.settings));
        }
    }
    if (next != m_snapshot) {
        m_snapshot = std::move(next);
        emit snapshotChanged();
    }
}

void TuningWindowQml::applyValue(const QString& key, winshun::Json value)
{
    m_apply(rawOverridesFromJson(winshun::Json::Object {{key.toStdString(), std::move(value)}}, false));
}

bool TuningWindowQml::applyNumber(const QString& key, const QString& input)
{
    const auto fieldName = key.toStdString();
    SettingRange range;
    bool ok = false;
    const auto value = input.toLongLong(&ok);
    if (!ok || !settingRange(fieldName.c_str(), range) || value < range.lo || value > range.hi)
        return false;
    if (!m_snapshot.value(key).toMap().isEmpty()
        && m_snapshot.value(key).toMap().value(QStringLiteral("value")).toLongLong() != value)
        applyValue(key, winshun::Json(static_cast<long long>(value)));
    return true;
}

void TuningWindowQml::setSwitch(const QString& key, bool on)
{
    if (m_snapshot.value(key).typeId() == QMetaType::Bool)
        applyValue(key, winshun::Json(on));
}

void TuningWindowQml::disableAcceleration()
{
    applyValue(QStringLiteral("impactAccelPps2"), winshun::Json(0));
}

void TuningWindowQml::applyAdvice()
{
    const auto pps = m_snapshot.value(QStringLiteral("advicePps")).toLongLong();
    if (pps > 0 && !m_snapshot.value(QStringLiteral("adviceFits")).toBool())
        applyValue(QStringLiteral("impactSpeedPps"), winshun::Json(static_cast<long long>(pps)));
}

void TuningWindowQml::resetDefaults()
{
    const Settings defaults;
    winshun::Json::Object values;
    for (const auto& field : settingsFields()) {
        if (field.owner == FieldOwner::Plugin && field.kind == FieldKind::Number)
            values.emplace(field.key, winshun::Json(field.number(defaults)));
    }
    m_apply(rawOverridesFromJson(winshun::Json(std::move(values)), false));
}

} // namespace cw
