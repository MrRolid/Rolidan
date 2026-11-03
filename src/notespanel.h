#pragma once

#include <QDockWidget>

class Document;
class QAction;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;

// Text notes about the image (Markdown): a side panel with an editor and a
// preview, import / export of .md files. Notes are part of the document
// state, so they have undo and are saved in projects and crash recovery,
// but they are never drawn into the image.
class NotesPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit NotesPanel(Document *doc, QWidget *parent = nullptr);

    QPlainTextEdit *editor() const { return m_editor; }
    QAction *previewAction() const { return m_preview; }

    // Set the notes from a file's text: `append` adds below the current notes.
    void importText(const QString &text, bool append);
    // Asks Replace / Append when there are notes already. False if cancelled.
    bool importFile(const QString &path);
    bool exportFile(const QString &path) const;
    static bool isNotesFile(const QString &path);

    void importDialog();
    void exportDialog();

private:
    void syncFromDocument();
    void editorChanged();
    void setPreview(bool on);

    Document *m_doc;
    QStackedWidget *m_stack;
    QPlainTextEdit *m_editor;
    QTextBrowser *m_browser;
    QAction *m_preview;
    QAction *m_import;
    QAction *m_export;
    bool m_syncing = false;
};
