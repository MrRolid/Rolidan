#pragma once

#include <QImage>
#include <QObject>
#include <QVariantMap>

// Screen capture (Etapa 7). Uses the desktop portal
// (org.freedesktop.portal.Screenshot: GNOME, KDE, Wayland and X11) and falls
// back to grabbing the X11 screens directly.
class ScreenCapture : public QObject
{
    Q_OBJECT
public:
    explicit ScreenCapture(QObject *parent = nullptr);

    // `interactive`: let the desktop ask for an area or window (portal only).
    void start(bool interactive);
    // True when the screens were grabbed directly (no area chosen yet).
    bool direct() const { return m_direct; }

    static bool portalAvailable();
    static bool canGrabDirectly(); // X11
    // The object path the portal uses for a request (to subscribe before calling).
    static QString requestPath(const QString &uniqueName, const QString &token);
    // Image of a portal "Response" (response code, results); null on cancel or error.
    static QImage imageFromResponse(uint response, const QVariantMap &results, QString *error);
    // All screens of the X11 desktop in one image.
    static QImage grabScreens();

signals:
    void captured(const QImage &image);
    void failed(const QString &message); // empty: cancelled by the user

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    void finishDirect();
    QString m_path;
    bool m_done = false;
    bool m_direct = false;
};

struct CaptureOptions
{
    bool interactive = false;
    int delaySeconds = 0;
    bool hideWindow = true;

    static CaptureOptions load();
    void save() const;
};
