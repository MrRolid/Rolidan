#include "history.h"

#include <QPainter>
#include <QSet>
#include <cstring>

namespace {
// A patch is worth it only if clearly smaller than the whole image.
constexpr double kPatchMaxFraction = 0.6;

QImage withPatch(const QImage &base, const QRect &r, const QImage &pixels)
{
    QImage out = base.copy();
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(r.topLeft(), pixels);
    p.end();
    return out;
}
} // namespace

QRect History::diffRect(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.format() != b.format() || a.isNull())
        return QRect(QPoint(0, 0), a.size().expandedTo(b.size()));
    const int h = a.height(), w = a.width();
    const int bpp = a.depth() / 8;
    const qsizetype lineBytes = qsizetype(w) * bpp;
    auto rowDiffers = [&](int y) { return std::memcmp(a.constScanLine(y), b.constScanLine(y), size_t(lineBytes)) != 0; };
    int top = 0;
    while (top < h && !rowDiffers(top))
        ++top;
    if (top == h)
        return {};
    int bottom = h - 1;
    while (bottom > top && !rowDiffers(bottom))
        --bottom;
    int left = w, right = -1;
    for (int y = top; y <= bottom; ++y) {
        const uchar *la = a.constScanLine(y), *lb = b.constScanLine(y);
        int x = 0;
        while (x < left && std::memcmp(la + x * bpp, lb + x * bpp, size_t(bpp)) == 0)
            ++x;
        left = std::min(left, x);
        int xr = w - 1;
        while (xr > right && std::memcmp(la + xr * bpp, lb + xr * bpp, size_t(bpp)) == 0)
            --xr;
        right = std::max(right, xr);
    }
    if (right < left)
        return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

History::Entry History::makeEntry(const QString &text, const QImage &prev, const DocState &state, int mergeKey) const
{
    Entry e;
    e.text = text;
    e.objects = state.objects;
    e.notes = state.notes;
    e.mergeKey = mergeKey;
    const QImage &next = state.base;
    if (prev.cacheKey() == next.cacheKey() || (prev.isNull() && next.isNull())) {
        e.kind = Entry::Kind::Same;
        return e;
    }
    if (!prev.isNull() && !next.isNull() && prev.size() == next.size() && prev.format() == next.format()) {
        const QRect r = diffRect(prev, next);
        if (r.isEmpty()) {
            e.kind = Entry::Kind::Same;
            return e;
        }
        if (double(r.width()) * r.height() <= kPatchMaxFraction * double(next.width()) * next.height()) {
            e.kind = Entry::Kind::Patch;
            e.rect = r;
            e.before = prev.copy(r);
            e.after = next.copy(r);
            return e;
        }
    }
    e.kind = Entry::Kind::Full;
    e.full = next;
    return e;
}

QImage History::baseAt(int index) const
{
    if (index == m_index)
        return m_current.base;
    int j = index;
    while (j > 0 && m_entries[j].kind != Entry::Kind::Full)
        --j;
    QImage base = m_entries[j].full;
    bool copied = false;
    for (int k = j + 1; k <= index; ++k) {
        const Entry &e = m_entries[k];
        if (e.kind == Entry::Kind::Patch) {
            if (!copied) {
                base = base.copy();
                copied = true;
            }
            QPainter p(&base);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(e.rect.topLeft(), e.after);
        }
    }
    return base;
}

void History::reset(const DocState &initial)
{
    m_entries.clear();
    Entry e;
    e.kind = Entry::Kind::Full;
    e.full = initial.base;
    e.objects = initial.objects;
    e.notes = initial.notes;
    m_entries.append(e);
    m_current = initial;
    m_index = 0;
    m_cleanIndex = 0;
    m_mergeOpen = false;
}

void History::push(const QString &text, const DocState &state, int mergeKey)
{
    // Drop redo branch.
    while (m_entries.size() > m_index + 1)
        m_entries.removeLast();
    if (m_cleanIndex > m_index)
        m_cleanIndex = -1;

    if (mergeKey != 0 && m_mergeOpen && m_index > 0 && m_entries[m_index].mergeKey == mergeKey
        && m_cleanIndex != m_index) {
        // Replace the last step: diff against the step before it.
        Entry &last = m_entries[m_index];
        if (last.kind == Entry::Kind::Same && state.base.cacheKey() == m_current.base.cacheKey()) {
            last.objects = state.objects; // the usual case: dragging a property slider
            last.notes = state.notes;     // or typing notes
        } else {
            QImage previous;
            switch (last.kind) {
            case Entry::Kind::Same: previous = m_current.base; break;
            case Entry::Kind::Patch: previous = withPatch(m_current.base, last.rect, last.before); break;
            case Entry::Kind::Full: previous = baseAt(m_index - 1); break;
            }
            last = makeEntry(last.text, previous, state, mergeKey);
        }
    } else {
        m_entries.append(makeEntry(text, m_current.base, state, mergeKey));
        ++m_index;
    }
    m_current = state;
    m_mergeOpen = mergeKey != 0;
    trim();
}

const DocState &History::undo()
{
    m_mergeOpen = false;
    if (!canUndo())
        return m_current;
    const Entry &e = m_entries[m_index];
    QImage base;
    switch (e.kind) {
    case Entry::Kind::Same: base = m_current.base; break;
    case Entry::Kind::Patch: base = withPatch(m_current.base, e.rect, e.before); break;
    case Entry::Kind::Full: base = baseAt(m_index - 1); break;
    }
    --m_index;
    m_current = DocState{base, m_entries[m_index].objects, m_entries[m_index].notes};
    return m_current;
}

const DocState &History::redo()
{
    m_mergeOpen = false;
    if (!canRedo())
        return m_current;
    const Entry &e = m_entries[m_index + 1];
    QImage base;
    switch (e.kind) {
    case Entry::Kind::Same: base = m_current.base; break;
    case Entry::Kind::Patch: base = withPatch(m_current.base, e.rect, e.after); break;
    case Entry::Kind::Full: base = e.full; break;
    }
    ++m_index;
    m_current = DocState{base, e.objects, e.notes};
    return m_current;
}

void History::setLimitBytes(qint64 bytes)
{
    m_limit = bytes;
    trim();
}

qint64 History::memoryUsage() const
{
    QSet<qint64> seen;
    qint64 total = 0;
    auto image = [&](const QImage &img) {
        if (!img.isNull() && !seen.contains(img.cacheKey())) {
            seen.insert(img.cacheKey());
            total += img.sizeInBytes();
        }
    };
    auto objects = [&](const ObjectList &list) {
        for (const auto &o : list) {
            const qint64 key = o->sharedDataKey();
            if (key == 0) {
                total += o->memoryBytes();
            } else if (!seen.contains(key)) {
                seen.insert(key);
                total += o->memoryBytes();
            }
        }
    };
    image(m_current.base);
    objects(m_current.objects);
    for (const Entry &e : m_entries) {
        image(e.full);
        if (e.kind == Entry::Kind::Patch)
            total += e.before.sizeInBytes() + e.after.sizeInBytes();
        objects(e.objects);
    }
    return total;
}

void History::trim()
{
    // Always keep the current step; drop the oldest ones first.
    while (m_index > 0 && memoryUsage() > m_limit) {
        if (m_entries[1].kind != Entry::Kind::Full) {
            // The new oldest step needs its own full image.
            m_entries[1].full = baseAt(1);
            m_entries[1].kind = Entry::Kind::Full;
            m_entries[1].before = QImage();
            m_entries[1].after = QImage();
        }
        m_entries.removeFirst();
        --m_index;
        m_cleanIndex = m_cleanIndex > 0 ? m_cleanIndex - 1 : -1;
    }
}
