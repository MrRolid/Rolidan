#include "toast.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

Toast::Toast(QWidget *parent)
    : QFrame(parent)
    , m_label(new QLabel(this))
    , m_button(new QPushButton(this))
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("toast"));
    setStyleSheet(QStringLiteral(
        "#toast { background: rgba(32,32,32,230); border-radius: 6px; }"
        "#toast QLabel { color: white; }"));
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 8, 8, 8);
    lay->addWidget(m_label);
    lay->addWidget(m_button);
    m_label->setWordWrap(true);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
    connect(m_button, &QPushButton::clicked, this, [this] {
        hide();
        if (m_action)
            m_action();
    });
    parent->installEventFilter(this);
    hide();
}

void Toast::showMessage(const QString &text, const QString &actionText, std::function<void()> action)
{
    m_label->setText(text);
    m_action = std::move(action);
    m_button->setText(actionText);
    m_button->setVisible(!actionText.isEmpty() && m_action);
    m_label->setMaximumWidth(qMax(200, parentWidget()->width() - 160));
    adjustSize();
    reposition();
    show();
    raise();
    m_timer->start(m_button->isVisible() ? 8000 : 4000);
}

QString Toast::text() const
{
    return isVisible() ? m_label->text() : QString();
}

bool Toast::eventFilter(QObject *o, QEvent *e)
{
    if (o == parentWidget() && e->type() == QEvent::Resize && isVisible())
        reposition();
    return QFrame::eventFilter(o, e);
}

void Toast::reposition()
{
    const QWidget *p = parentWidget();
    move((p->width() - width()) / 2, p->height() - height() - 24);
}
