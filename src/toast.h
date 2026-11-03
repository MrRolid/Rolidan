#pragma once

#include <QFrame>
#include <functional>

class QLabel;
class QPushButton;
class QTimer;

// Short non-modal notification shown at the bottom of the canvas, optionally
// with one action button (e.g. "Expand canvas", spec B2).
class Toast : public QFrame
{
    Q_OBJECT
public:
    explicit Toast(QWidget *parent);

    void showMessage(const QString &text, const QString &actionText = {},
                     std::function<void()> action = {});
    QString text() const;

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void reposition();

    QLabel *m_label;
    QPushButton *m_button;
    QTimer *m_timer;
    std::function<void()> m_action;
};
