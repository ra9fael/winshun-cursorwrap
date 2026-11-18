#include "theme_bridge.h"

#include <QColor>
#include <QGuiApplication>
#include <QPalette>

namespace cw {

ThemeBridge::ThemeBridge()
{
    apply(QStringLiteral("light"), {});
}

void ThemeBridge::apply(const QString& theme, const QVariantMap& appearance)
{
    const auto look = appearance.value(QStringLiteral("nativeLook")).toMap();
    m_dark = look.value(QStringLiteral("dark"), theme == QStringLiteral("dark")).toBool();
    m_rounded = look.value(QStringLiteral("rounded"),
        appearance.value(QStringLiteral("corners")).toString() != QStringLiteral("square")).toBool();
    m_backdropOn = appearance.value(QStringLiteral("transparency")).toString() == QStringLiteral("on");
    m_cardRadius = m_rounded ? look.value(QStringLiteral("cardRadius"), 6).toInt() : 0;
    m_controlRadius = m_rounded ? look.value(QStringLiteral("controlRadius"), 4).toInt() : 0;
    const QColor accent = QGuiApplication::palette().highlight().color();

    const auto put = [&](const char* key, QColor fallback) {
        const auto name = QString::fromLatin1(key);
        const auto token = QColor::fromString(look.value(name).toString());
        m_colors.insert(name, token.isValid() ? token : fallback);
    };
    put("background", QColor(m_dark ? "#202020" : "#F9F9F9"));
    put("layer", QColor(m_dark ? "#292929" : "#FFFFFF"));
    put("text", QColor(m_dark ? "#F3F3F3" : "#1B1B1B"));
    put("subtext", QColor(m_dark ? "#C5C5C5" : "#5D5D5D"));
    put("divider", QColor(m_dark ? "#404040" : "#E5E5E5"));
    put("control", QColor(m_dark ? "#333333" : "#FFFFFF"));
    put("controlHover", QColor(m_dark ? "#414141" : "#EFEFEF"));
    put("controlBorder", QColor(m_dark ? "#575757" : "#D0D0D0"));
    put("pressed", QColor(m_dark ? "#494949" : "#DFDFDF"));
    put("accent", m_dark ? accent.lighter(155) : accent);
    put("accentFill", m_dark ? accent.lighter(155) : accent);
    put("accentText", QColor(m_dark ? "#151515" : "#FFFFFF"));
    put("danger", QColor(m_dark ? "#FF99A4" : "#C42B1C"));
    emit changed();
}

} // namespace cw
