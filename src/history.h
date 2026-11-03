#pragma once

#include "annotation.h"

#include <QImage>
#include <QList>
#include <QString>

// A full snapshot of document content. Cheap to copy: QImage is implicitly
// shared and objects are immutable shared pointers, so unchanged parts are
// shared between history steps.
struct DocState
{
    QImage base;
    ObjectList objects;
    QString notes; // text notes about the image (Markdown), not drawn
};

// Linear undo history with a memory limit (spec G1). Steps are exposed as
// full snapshots, but stored compactly: a step that changes only part of the
// base keeps just the changed rectangle (before and after), a step that does
// not touch the base keeps nothing but its objects. Full images are kept only
// where the size changes (and for the oldest step). Objects are immutable and
// shared between steps.
class History
{
public:
    void reset(const DocState &initial);
    void push(const QString &text, const DocState &state, int mergeKey = 0);
    void closeMerge() { m_mergeOpen = false; }

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_entries.size(); }
    const DocState &undo();
    const DocState &redo();
    const DocState &current() const { return m_current; }
    QString undoText() const { return canUndo() ? m_entries[m_index].text : QString(); }
    QString redoText() const { return canRedo() ? m_entries[m_index + 1].text : QString(); }

    void setClean() { m_cleanIndex = m_index; }
    bool isClean() const { return m_cleanIndex == m_index; }

    void setLimitBytes(qint64 bytes);
    qint64 limitBytes() const { return m_limit; }
    qint64 memoryUsage() const;
    int count() const { return m_entries.size(); }
    int index() const { return m_index; }

    // Bounding rectangle of differing pixels (same size and format), empty if equal.
    static QRect diffRect(const QImage &a, const QImage &b);

private:
    struct Entry
    {
        enum class Kind { Full, Same, Patch };
        QString text;
        ObjectList objects;
        QString notes;
        Kind kind = Kind::Full;
        QImage full;          // Kind::Full
        QRect rect;           // Kind::Patch: changed area
        QImage before, after; // Kind::Patch: pixels of that area
        int mergeKey = 0;
    };

    Entry makeEntry(const QString &text, const QImage &previousBase, const DocState &state, int mergeKey) const;
    QImage baseAt(int index) const; // reconstruct from the nearest full image
    void trim();

    QList<Entry> m_entries;
    DocState m_current;
    int m_index = -1;
    int m_cleanIndex = -1;
    bool m_mergeOpen = false;
    qint64 m_limit = qint64(1024) * 1024 * 1024;
};
