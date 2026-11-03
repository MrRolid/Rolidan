#include "notespanel.h"

#include "document.h"
#include "icons.h"
#include "settings.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int kNotesMergeKey = 3000; // consecutive typing = one undo step
}

NotesPanel::NotesPanel(Document *doc, QWidget *parent)
    : QDockWidget(tr("Notes"), parent)
    , m_doc(doc)
{
    setObjectName(QStringLiteral("notesPanel"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    auto *box = new QWidget(this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QToolBar(box);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_preview = bar->addAction(icons::get(QStringLiteral("notes-preview")), tr("Preview"));
    m_preview->setCheckable(true);
    m_preview->setToolTip(tr("Show the notes formatted (Markdown)"));
    connect(m_preview, &QAction::toggled, this, &NotesPanel::setPreview);
    bar->addSeparator();
    m_import = bar->addAction(icons::get(QStringLiteral("document-open")), tr("Import…"));
    m_import->setToolTip(tr("Load notes from a Markdown or text file (.md, .txt)"));
    connect(m_import, &QAction::triggered, this, &NotesPanel::importDialog);
    m_export = bar->addAction(icons::get(QStringLiteral("document-save")), tr("Export…"));
    m_export->setToolTip(tr("Save the notes as a Markdown file (.md)"));
    connect(m_export, &QAction::triggered, this, &NotesPanel::exportDialog);
    layout->addWidget(bar);

    m_stack = new QStackedWidget(box);
    m_editor = new QPlainTextEdit(m_stack);
    m_editor->setPlaceholderText(tr("Notes about this image (Markdown). They are saved in the project "
                                    "(.rolidan), not drawn into the image.\n\nImport a .md file with "
                                    "the button above or drop it on the window."));
    m_editor->setTabChangesFocus(false);
    m_browser = new QTextBrowser(m_stack);
    m_browser->setOpenExternalLinks(true);
    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_browser);
    layout->addWidget(m_stack, 1);
    setWidget(box);

    connect(m_editor, &QPlainTextEdit::textChanged, this, &NotesPanel::editorChanged);
    connect(doc, &Document::changed, this, &NotesPanel::syncFromDocument);
    connect(doc, &Document::documentReset, this, [this] {
        syncFromDocument();
        if (!m_doc->state().notes.isEmpty()) {
            show(); // an opened project with notes: show them
            setPreview(true);
            m_preview->setChecked(true);
        }
    });
    syncFromDocument();
}

void NotesPanel::syncFromDocument()
{
    const bool enabled = !m_doc->isEmpty();
    m_editor->setReadOnly(!enabled);
    m_import->setEnabled(enabled);
    m_export->setEnabled(enabled && !m_doc->state().notes.isEmpty());
    const QString &notes = m_doc->state().notes;
    if (m_editor->toPlainText() != notes) {
        m_syncing = true;
        const int pos = m_editor->textCursor().position();
        m_editor->setPlainText(notes);
        QTextCursor c = m_editor->textCursor();
        c.setPosition(std::min(pos, int(notes.size())));
        m_editor->setTextCursor(c);
        m_syncing = false;
    }
    if (m_stack->currentWidget() == m_browser)
        m_browser->setMarkdown(notes);
}

void NotesPanel::editorChanged()
{
    if (m_syncing || m_doc->isEmpty())
        return;
    DocState s = m_doc->state();
    s.notes = m_editor->toPlainText();
    if (s.notes == m_doc->state().notes)
        return;
    m_doc->commit(s, tr("Edit notes"), kNotesMergeKey);
}

void NotesPanel::setPreview(bool on)
{
    if (on)
        m_browser->setMarkdown(m_doc->state().notes);
    m_doc->closeMerge();
    m_stack->setCurrentWidget(on ? static_cast<QWidget *>(m_browser) : m_editor);
    if (m_preview->isChecked() != on)
        m_preview->setChecked(on);
}

void NotesPanel::importText(const QString &textIn, bool append)
{
    if (m_doc->isEmpty())
        return;
    QString text = textIn;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    DocState s = m_doc->state();
    if (append && !s.notes.isEmpty()) {
        s.notes += s.notes.endsWith(QLatin1Char('\n')) ? QStringLiteral("\n") : QStringLiteral("\n\n");
        s.notes += text;
    } else {
        s.notes = text;
    }
    m_doc->closeMerge();
    m_doc->commit(s, tr("Import notes"));
    show();
    raise();
}

bool NotesPanel::isNotesFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown");
}

bool NotesPanel::importFile(const QString &path)
{
    if (m_doc->isEmpty())
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Notes"), tr("Cannot read %1: %2").arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.read(16 * 1024 * 1024); // a text file, not a dump
    bool append = false;
    if (!m_doc->state().notes.trimmed().isEmpty()) {
        QMessageBox box(QMessageBox::Question, tr("Import Notes"),
                        tr("The image already has notes. Replace them with “%1”, or add it below?")
                            .arg(QFileInfo(path).fileName()),
                        QMessageBox::Cancel, this);
        QPushButton *add = box.addButton(tr("Add Below"), QMessageBox::AcceptRole);
        QPushButton *replace = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
        box.setDefaultButton(add);
        box.exec();
        if (box.clickedButton() != add && box.clickedButton() != replace)
            return false;
        append = box.clickedButton() == add;
    }
    importText(QString::fromUtf8(data), append);
    return true;
}

bool NotesPanel::exportFile(const QString &path) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QByteArray data = m_doc->state().notes.toUtf8();
    if (!data.endsWith('\n'))
        data.append('\n');
    f.write(data);
    return f.commit();
}

void NotesPanel::importDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Notes"), settings::lastDirectory(),
        tr("Markdown and text (*.md *.markdown *.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    settings::setLastDirectory(QFileInfo(path).absolutePath());
    importFile(path);
}

void NotesPanel::exportDialog()
{
    QString start;
    const QString source = !m_doc->filePath().isEmpty() ? m_doc->filePath() : m_doc->originPath();
    if (!source.isEmpty())
        start = QFileInfo(source).absolutePath() + QLatin1Char('/') + QFileInfo(source).completeBaseName()
                + QStringLiteral(".md");
    else
        start = settings::lastDirectory() + QStringLiteral("/notes.md");
    QString path = QFileDialog::getSaveFileName(this, tr("Export Notes"), start,
                                                tr("Markdown (*.md);;Text (*.txt)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".md");
    if (!exportFile(path))
        QMessageBox::warning(this, tr("Export Notes"), tr("Cannot write to %1.").arg(path));
}
