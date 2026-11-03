#pragma once

#include <QApplication>
#include <QList>
#include <QPointer>

class MainWindow;
class QTimer;

class Application : public QApplication
{
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    static Application *instance() { return static_cast<Application *>(QCoreApplication::instance()); }

    // Parse the command line (spec B6) and open the first windows.
    // Returns false if the process should exit immediately.
    bool start();

    MainWindow *newWindow();
    void openInNewWindow(const QString &path);
    void newWindowFromClipboard();
    int windowCount() const;
    bool isLingering() const { return m_lingering; }

private:
    void onWindowDestroyed(QObject *window);
    void checkLinger();

    QList<QPointer<MainWindow>> m_windows;
    QTimer *m_lingerTimer;
    bool m_lingering = false;
};
