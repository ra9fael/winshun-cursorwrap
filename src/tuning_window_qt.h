#pragma once

#include "settings_store.h"
#include "theme_bridge.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

class QQmlApplicationEngine;
class QQuickWindow;
namespace ws { class WindowFrame; }

namespace cw {

// What the tuning window displays; filled by the plugin on demand (the
// window reads this, never the engine's internals).
struct SettingsUiState {
    Settings settings;   // the values in effect right now
    GateAdvice advice;   // the suggested gate (adviseGate)
    std::string summary; // the host card's keys, read-only, one line
    size_t arrivals = 0; // trace rows behind the suggestion
    bool chinese = true;
};

class TuningWindowQml final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(bool backdrop READ backdrop NOTIFY backdropChanged FINAL)

public:
    using Provider = std::function<SettingsUiState()>;
    using Apply = std::function<void(const RawSettings&)>;

    TuningWindowQml(Provider provider, Apply apply);
    ~TuningWindowQml() override;
    void show();
    void shutdown();
    void settingsUpdated();
    void applyTheme(const QString& theme, const QVariantMap& appearance);

    const QVariantMap& snapshot() const { return m_snapshot; }
    bool backdrop() const { return m_backdrop; }

    Q_INVOKABLE bool applyNumber(const QString& key, const QString& input);
    Q_INVOKABLE void setSwitch(const QString& key, bool on);
    Q_INVOKABLE void disableAcceleration();
    Q_INVOKABLE void applyAdvice();
    Q_INVOKABLE void resetDefaults();

signals:
    void snapshotChanged();
    void backdropChanged();

private:
    void updateFrame();
    void applyValue(const QString& key, winshun::Json value);

    Provider m_provider;
    Apply m_apply;
    ThemeBridge m_theme;
    QVariantMap m_snapshot;
    QTimer m_timer;
    // The frame's dark mode as the theme asks for it, put back while Qt's own
    // light one is on its way (see applyTheme).
    QTimer m_darkFrameGuard;
    QElapsedTimer m_darkFrameWatch;
    bool m_dark = false;
    bool m_backdrop = false;
    QQuickWindow* m_window = nullptr;
    std::unique_ptr<ws::WindowFrame> m_frame;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

} // namespace cw
