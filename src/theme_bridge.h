#pragma once
#include <QObject>
#include <QVariantMap>

namespace cw {

class ThemeBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed FINAL)
    Q_PROPERTY(int cardRadius MEMBER m_cardRadius NOTIFY changed FINAL)
    Q_PROPERTY(int controlRadius MEMBER m_controlRadius NOTIFY changed FINAL)
    Q_PROPERTY(bool rounded READ rounded NOTIFY changed FINAL)

public:
    ThemeBridge();
    void apply(const QString& theme, const QVariantMap& appearance);
    const QVariantMap& colors() const { return m_colors; }
    bool rounded() const { return m_rounded; }
    bool dark() const { return m_dark; }
    bool backdropOn() const { return m_backdropOn; }

signals:
    void changed();

private:
    QVariantMap m_colors;
    int m_cardRadius = 6;
    int m_controlRadius = 4;
    bool m_rounded = true;
    bool m_dark = false;
    bool m_backdropOn = false;
};

} // namespace cw
