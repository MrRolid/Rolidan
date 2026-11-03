#include "application.h"

#include "canvas.h"
#include "clipboard.h"
#include "i18n.h"
#include "imageio.h"
#include "mainwindow.h"
#include "recovery.h"
#include "settings.h"
#include "toast.h"

#include <QClipboard>
#include <QCommandLineParser>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <cstdio>

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
    , m_lingerTimer(new QTimer(this))
{
    setApplicationName(QStringLiteral("Rolidan"));
    setOrganizationName(QStringLiteral("Rolidan"));
    setApplicationVersion(QStringLiteral(ROLIDAN_VERSION));
    setDesktopFileName(QStringLiteral("rolidan")); // Wayland app_id (spec B6)
    setQuitOnLastWindowClosed(false);              // we decide ourselves (spec B4)
    imageio::initialize();
    i18n::install(settings::language());

    m_lingerTimer->setInterval(2000);
    connect(m_lingerTimer, &QTimer::timeout, this, &Application::checkLinger);
    connect(clipboard(), &QClipboard::dataChanged, this, [this] {
        if (m_lingering)
            checkLinger();
    });
}

bool Application::start()
{
    QCommandLineParser parser;
    parser.setApplicationDescription(tr("Fast image editor: paste, annotate, save."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption pasteOpt(QStringLiteral("paste"),
                                tr("Paste the clipboard into a new document after start."));
    parser.addOption(pasteOpt);
    parser.addPositionalArgument(QStringLiteral("files"),
                                 tr("Image files to open; \"-\" reads an image from standard input."),
                                 QStringLiteral("[files...]"));
    parser.process(*this);

    const QStringList files = parser.positionalArguments();
    bool any = false;

    // Crash recovery (spec F7): documents of a Rolidan that did not quit properly.
    const QStringList orphans = recovery::orphans();
    if (!orphans.isEmpty()) {
        QMessageBox box(QMessageBox::Question, tr("Restore"),
                        tr("Rolidan was not closed properly. Restore %n unsaved document(s)?", nullptr,
                           int(orphans.size())));
        QPushButton *restore = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
        QPushButton *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.setDefaultButton(restore);
        box.exec();
        if (box.clickedButton() == restore) {
            for (const QString &path : orphans) {
                MainWindow *w = newWindow();
                if (!w->restoreRecovery(path))
                    w->close();
                else
                    any = true;
            }
        } else if (box.clickedButton() == discard) {
            for (const QString &path : orphans)
                QFile::remove(path);
        }
    }
    for (const QString &f : files) {
        MainWindow *w = newWindow();
        if (f == QLatin1String("-")) {
            QFile in;
            in.open(stdin, QIODevice::ReadOnly);
            const imageio::LoadResult r = imageio::loadData(in.readAll());
            if (r.ok())
                w->openImage(r.image, {}, r.notes);
            else
                std::fprintf(stderr, "rolidan: stdin: %s\n", qPrintable(r.error));
        } else if (!w->openFile(f)) {
            std::fprintf(stderr, "rolidan: cannot open %s\n", qPrintable(f));
        }
        any = true;
    }
    if (parser.isSet(pasteOpt)) {
        MainWindow *w = newWindow();
        w->pasteWhenActive();
        any = true;
    }
    if (!any)
        newWindow();
    return true;
}

MainWindow *Application::newWindow()
{
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    connect(w, &QObject::destroyed, this, &Application::onWindowDestroyed);
    m_windows.append(w);
    if (m_lingering) {
        m_lingering = false;
        m_lingerTimer->stop();
    }
    // Cascade new windows slightly.
    if (m_windows.size() > 1 && m_windows[m_windows.size() - 2])
        w->move(m_windows[m_windows.size() - 2]->pos() + QPoint(32, 32));
    w->show();
    return w;
}

void Application::openInNewWindow(const QString &path)
{
    MainWindow *w = newWindow();
    if (!w->openFile(path))
        w->close();
}

void Application::newWindowFromClipboard()
{
    newWindow()->paste();
}

int Application::windowCount() const
{
    int n = 0;
    for (const auto &w : m_windows)
        n += w ? 1 : 0;
    return n;
}

void Application::onWindowDestroyed(QObject *window)
{
    // QPointer may not be cleared yet while `destroyed` is emitted: compare addresses.
    m_windows.removeIf([window](const QPointer<MainWindow> &w) {
        return w.isNull() || static_cast<QObject *>(w.data()) == window;
    });
    if (!m_windows.isEmpty())
        return;
    // Spec B4 / decision J4: keep serving the clipboard without a window.
    if (settings::keepClipboardAfterClose() && clip::ownsClipboard()) {
        m_lingering = true;
        m_lingerTimer->start();
        return;
    }
    quit();
}

void Application::checkLinger()
{
    if (m_lingering && m_windows.isEmpty() && !clip::ownsClipboard()) {
        m_lingerTimer->stop();
        m_lingering = false;
        quit();
    }
}
