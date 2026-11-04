#include "shortcuts.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHash>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace shortcuts {

namespace {
QString key(const QAction *act) { return QStringLiteral("shortcuts/") + act->objectName(); }

QList<QKeySequence> fromString(const QString &s)
{
    QList<QKeySequence> out;
    for (const QKeySequence &k : QKeySequence::listFromString(s, QKeySequence::PortableText))
        if (!k.isEmpty())
            out << k;
    return out;
}
} // namespace

void init(QAction *act)
{
    act->setProperty("defaultShortcuts", QKeySequence::listToString(act->shortcuts(), QKeySequence::PortableText));
    act->setShortcuts(current(act));
}

QList<QKeySequence> defaults(const QAction *act)
{
    const QVariant v = act->property("defaultShortcuts");
    return v.isValid() ? fromString(v.toString()) : act->shortcuts();
}

QList<QKeySequence> current(const QAction *act)
{
    if (act->objectName().isEmpty())
        return act->shortcuts();
    QSettings s;
    const QVariant v = s.value(key(act));
    return v.isValid() ? fromString(v.toString()) : defaults(act);
}

void set(QAction *act, const QList<QKeySequence> &keys)
{
    QSettings s;
    if (keys == defaults(act))
        s.remove(key(act));
    else
        s.setValue(key(act), QKeySequence::listToString(keys, QKeySequence::PortableText)); // "" = none
    act->setShortcuts(keys);
}

void reload(const QList<QAction *> &actions)
{
    for (QAction *a : actions)
        if (!a->objectName().isEmpty() && a->property("defaultShortcuts").isValid())
            a->setShortcuts(current(a));
}

QString text(const QList<QKeySequence> &keys)
{
    QStringList parts;
    for (const QKeySequence &k : keys)
        parts << k.toString(QKeySequence::NativeText);
    return parts.join(QStringLiteral(", "));
}

} // namespace shortcuts

namespace {
QString plainText(QString t)
{
    static const QRegularExpression cjk(QStringLiteral("\\(&[^)]\\)"));
    t.remove(cjk);
    t.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    t.remove(QLatin1Char('&'));
    t.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    if (t.endsWith(QStringLiteral("…")))
        t.chop(1);
    return t.trimmed();
}

QString categoryTitle(const QString &prefix)
{
    if (prefix == QLatin1String("file")) return ShortcutsDialog::tr("File");
    if (prefix == QLatin1String("edit")) return ShortcutsDialog::tr("Edit");
    if (prefix == QLatin1String("arrange")) return ShortcutsDialog::tr("Arrange");
    if (prefix == QLatin1String("image")) return ShortcutsDialog::tr("Image");
    if (prefix == QLatin1String("filter")) return ShortcutsDialog::tr("Colors and Filters");
    if (prefix == QLatin1String("tool")) return ShortcutsDialog::tr("Tools");
    if (prefix == QLatin1String("view")) return ShortcutsDialog::tr("View");
    return ShortcutsDialog::tr("Other");
}
} // namespace

ShortcutsDialog::ShortcutsDialog(const QList<QAction *> &actions, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    auto *main = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search actions or keys…"));
    m_search->setClearButtonEnabled(true);
    main->addWidget(m_search);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Action"), tr("Shortcut"), tr("Alternative")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    main->addWidget(m_tree, 1);

    // Group by the id prefix, in menu order.
    const QStringList order{QStringLiteral("file"), QStringLiteral("edit"), QStringLiteral("arrange"),
                            QStringLiteral("image"), QStringLiteral("filter"), QStringLiteral("tool"),
                            QStringLiteral("view"), QStringLiteral("help")};
    QMap<int, QList<QAction *>> groups;
    for (QAction *a : actions) {
        if (a->objectName().isEmpty())
            continue;
        const QString prefix = a->objectName().section(QLatin1Char('.'), 0, 0);
        int index = int(order.indexOf(prefix));
        groups[index < 0 ? int(order.size()) : index].append(a);
    }
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const QString prefix = it.value().first()->objectName().section(QLatin1Char('.'), 0, 0);
        auto *header = new QTreeWidgetItem(m_tree, {categoryTitle(prefix)});
        header->setFirstColumnSpanned(true);
        QFont f = header->font(0);
        f.setBold(true);
        header->setFont(0, f);
        header->setFlags(Qt::ItemIsEnabled);
        for (QAction *a : it.value()) {
            auto *item = new QTreeWidgetItem(m_tree, {plainText(a->text())});
            item->setIcon(0, a->icon());
            item->setData(0, Qt::UserRole, a->objectName());
            Row r{a, item, {}};
            const QList<QKeySequence> keys = shortcuts::current(a);
            for (int slot = 0; slot < 2; ++slot) {
                auto *edit = new QKeySequenceEdit(keys.value(slot), m_tree);
                edit->setMaximumSequenceLength(1);
                edit->setClearButtonEnabled(true);
                edit->setObjectName(a->objectName() + QStringLiteral("#%1").arg(slot));
                m_tree->setItemWidget(item, slot + 1, edit);
                r.edit[slot] = edit;
            }
            m_rows.append(r);
        }
    }
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 150);
    m_tree->header()->resizeSection(2, 150);
    m_tree->header()->setStretchLastSection(false);

    auto *note = new QLabel(tr("Click a field and press the new key. Tools keep working with Shift + their letter "
                               "unless you change that too. Esc, Enter, Space, the arrow keys and [ ] keep their "
                               "fixed meaning while a tool uses them."),
                            this);
    note->setWordWrap(true);
    note->setEnabled(false);
    main->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *reset = buttons->addButton(tr("Reset All to Defaults"), QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, &ShortcutsDialog::resetAll);
    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(buttons);
    connect(m_search, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);
    resize(640, 620);
}

QList<QKeySequence> ShortcutsDialog::keysOf(const Row &r) const
{
    QList<QKeySequence> keys;
    for (QKeySequenceEdit *e : r.edit) {
        const QKeySequence k = e->keySequence();
        if (!k.isEmpty() && !keys.contains(k))
            keys << k;
    }
    return keys;
}

void ShortcutsDialog::filter(const QString &text)
{
    const QString t = text.trimmed();
    QTreeWidgetItem *header = nullptr;
    bool headerHasRows = false;
    auto closeHeader = [&] {
        if (header)
            header->setHidden(!headerHasRows);
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString().isEmpty()) {
            closeHeader();
            header = item;
            headerHasRows = false;
            continue;
        }
        bool match = t.isEmpty() || item->text(0).contains(t, Qt::CaseInsensitive);
        for (const Row &r : std::as_const(m_rows))
            if (r.item == item && !match)
                match = shortcuts::text(keysOf(r)).contains(t, Qt::CaseInsensitive);
        item->setHidden(!match);
        headerHasRows |= match;
    }
    closeHeader();
}

void ShortcutsDialog::resetAll()
{
    for (const Row &r : std::as_const(m_rows)) {
        const QList<QKeySequence> d = shortcuts::defaults(r.action);
        for (int slot = 0; slot < 2; ++slot)
            r.edit[slot]->setKeySequence(d.value(slot));
    }
}

void ShortcutsDialog::setKeys(const QString &objectName, int slot, const QKeySequence &key)
{
    for (const Row &r : std::as_const(m_rows))
        if (r.action->objectName() == objectName)
            r.edit[std::clamp(slot, 0, 1)]->setKeySequence(key);
}

QStringList ShortcutsDialog::conflicts() const
{
    QMap<QString, QStringList> byKey;
    for (const Row &r : m_rows)
        for (const QKeySequence &k : keysOf(r))
            byKey[k.toString(QKeySequence::NativeText)] << r.item->text(0);
    QStringList out;
    for (auto it = byKey.begin(); it != byKey.end(); ++it)
        if (it.value().size() > 1)
            out << QStringLiteral("%1: %2").arg(it.key(), it.value().join(QStringLiteral(", ")));
    return out;
}

void ShortcutsDialog::accept()
{
    const QStringList c = conflicts();
    if (!c.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Some keys are used by more than one action:\n\n%1\n\nChange them first.")
                                 .arg(c.join(QLatin1Char('\n'))));
        return;
    }
    for (const Row &r : std::as_const(m_rows)) {
        shortcuts::set(r.action, keysOf(r));
    }
    QDialog::accept();
}
