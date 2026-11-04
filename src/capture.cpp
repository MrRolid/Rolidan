#include "capture.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QUrl>

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kObject = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
} // namespace

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
{
}

bool ScreenCapture::portalAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    if (bus.interface()->isServiceRegistered(kService))
        return true;
    // The portal is usually started on demand.
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                             QStringLiteral("/org/freedesktop/DBus"),
                                                             QStringLiteral("org.freedesktop.DBus"),
                                                             QStringLiteral("ListActivatableNames"));
    const QDBusReply<QStringList> names = bus.call(call, QDBus::Block, 2000);
    return names.isValid() && names.value().contains(kService);
}

bool ScreenCapture::canGrabDirectly()
{
    return QGuiApplication::platformName() == QLatin1String("xcb");
}

QString ScreenCapture::requestPath(const QString &uniqueName, const QString &token)
{
    QString sender = uniqueName;
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

QImage ScreenCapture::imageFromResponse(uint response, const QVariantMap &results, QString *error)
{
    if (error)
        error->clear();
    if (response == 1) // cancelled by the user
        return {};
    if (response != 0) {
        if (error)
            *error = tr("The desktop did not take the screenshot.");
        return {};
    }
    const QUrl url(results.value(QStringLiteral("uri")).toString());
    QImage img;
    if (url.isLocalFile())
        img.load(url.toLocalFile());
    if (img.isNull() && error)
        *error = tr("The screenshot could not be read.");
    return img.isNull() ? QImage() : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage ScreenCapture::grabScreens()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return {};
    QRect all;
    for (QScreen *s : screens)
        all |= s->geometry();
    const double dpr = screens.first()->devicePixelRatio();
    QImage out((all.size() * dpr), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::black);
    QPainter p(&out);
    for (QScreen *s : screens) {
        const QPixmap pm = s->grabWindow(0);
        if (pm.isNull())
            continue;
        const QRect g = s->geometry();
        p.drawPixmap(QRectF(QPointF(g.topLeft() - all.topLeft()) * dpr, QSizeF(g.size()) * dpr), pm,
                     QRectF(pm.rect()));
    }
    p.end();
    return out;
}

void ScreenCapture::start(bool interactive)
{
    m_done = false;
    if (!portalAvailable()) {
        if (canGrabDirectly())
            finishDirect();
        else
            emit failed(tr("Screenshots need the desktop portal (xdg-desktop-portal), which is not running."));
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("rolidan%1").arg(QRandomGenerator::global()->generate());
    // Subscribe before calling, so a quick answer is not missed.
    m_path = requestPath(bus.baseService(), token);
    bus.connect(kService, m_path, kRequest, QStringLiteral("Response"), this,
                SLOT(onResponse(uint, QVariantMap)));
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kObject, QStringLiteral("org.freedesktop.portal.Screenshot"),
                                                       QStringLiteral("Screenshot"));
    call << QString() << QVariantMap{{QStringLiteral("handle_token"), token},
                                     {QStringLiteral("interactive"), interactive},
                                     {QStringLiteral("modal"), true}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, bus](QDBusPendingCallWatcher *w) mutable {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            bus.disconnect(kService, m_path, kRequest, QStringLiteral("Response"), this,
                           SLOT(onResponse(uint, QVariantMap)));
            if (canGrabDirectly())
                finishDirect();
            else
                emit failed(tr("The desktop refused the screenshot: %1").arg(reply.error().message()));
            return;
        }
        const QString path = reply.value().path();
        if (path != m_path && !m_done) { // older portals choose their own path
            bus.disconnect(kService, m_path, kRequest, QStringLiteral("Response"), this,
                           SLOT(onResponse(uint, QVariantMap)));
            m_path = path;
            bus.connect(kService, m_path, kRequest, QStringLiteral("Response"), this,
                        SLOT(onResponse(uint, QVariantMap)));
        }
    });
}

void ScreenCapture::onResponse(uint response, const QVariantMap &results)
{
    if (m_done)
        return;
    m_done = true;
    QDBusConnection::sessionBus().disconnect(kService, m_path, kRequest, QStringLiteral("Response"), this,
                                             SLOT(onResponse(uint, QVariantMap)));
    QString error;
    const QImage img = imageFromResponse(response, results, &error);
    if (img.isNull())
        emit failed(error);
    else
        emit captured(img);
}

void ScreenCapture::finishDirect()
{
    m_done = true;
    m_direct = true;
    const QImage img = grabScreens();
    if (img.isNull())
        emit failed(tr("The screen could not be captured."));
    else
        emit captured(img);
}

// ---------------------------------------------------------------- options

CaptureOptions CaptureOptions::load()
{
    QSettings s;
    CaptureOptions o;
    o.interactive = s.value(QStringLiteral("capture/interactive"), false).toBool();
    o.delaySeconds = std::clamp(s.value(QStringLiteral("capture/delay"), 0).toInt(), 0, 60);
    o.hideWindow = s.value(QStringLiteral("capture/hideWindow"), true).toBool();
    return o;
}

void CaptureOptions::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("capture/interactive"), interactive);
    s.setValue(QStringLiteral("capture/delay"), delaySeconds);
    s.setValue(QStringLiteral("capture/hideWindow"), hideWindow);
}
