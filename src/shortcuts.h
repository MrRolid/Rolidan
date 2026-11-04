#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>

class QAction;
class QKeySequenceEdit;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Customizable keyboard shortcuts (Etapa 5). Every action with an objectName
// ("file.open", "tool.rect", …) can get its own shortcuts, stored in the
// settings under "shortcuts/<objectName>"; actions without one keep theirs.
namespace shortcuts {

// Remember the current shortcuts of `act` as its defaults, then apply the user's.
void init(QAction *act);
QList<QKeySequence> defaults(const QAction *act);
// The user's shortcuts (the defaults if not customized).
QList<QKeySequence> current(const QAction *act);
// Store (or clear, when equal to the defaults) and apply.
void set(QAction *act, const QList<QKeySequence> &keys);
// Re-read the settings for these actions (after another window changed them).
void reload(const QList<QAction *> &actions);
// Human-readable list: "R, >"
QString text(const QList<QKeySequence> &keys);

} // namespace shortcuts

class ShortcutsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutsDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);
    void accept() override;

    // For tests: edit the shortcut column `slot` (0 main, 1 alternative) of an action.
    void setKeys(const QString &objectName, int slot, const QKeySequence &key);
    // Pairs of actions that share a shortcut, as "key: action, action" lines.
    QStringList conflicts() const;

private:
    struct Row
    {
        QAction *action;
        QTreeWidgetItem *item;
        QKeySequenceEdit *edit[2];
    };
    void filter(const QString &text);
    void resetAll();
    QList<QKeySequence> keysOf(const Row &r) const;

    QTreeWidget *m_tree;
    QLineEdit *m_search;
    QList<Row> m_rows;
};
