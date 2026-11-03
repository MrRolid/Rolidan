#include "settingsdialog.h"

#include "i18n.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_language(new QComboBox(this))
    , m_keepClipboard(new QCheckBox(tr("Keep clipboard contents after closing the window"), this))
    , m_historyLimit(new QSpinBox(this))
{
    setWindowTitle(tr("Settings"));
    auto *main = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    main->addLayout(form);

    const QString current = settings::language();
    for (const auto &lang : i18n::available()) {
        m_language->addItem(lang.builtIn ? lang.name : tr("%1 (external file)").arg(lang.name), lang.code);
        if (lang.code == current)
            m_language->setCurrentIndex(m_language->count() - 1);
    }
    form->addRow(tr("Language:"), m_language);
    auto *langNote = new QLabel(tr("Takes effect after restarting the application. Additional "
                                   "languages are loaded from rolidan_<code>.qm files in:\n%1")
                                    .arg(i18n::externalDirectories().join(QLatin1Char('\n'))),
                                this);
    langNote->setWordWrap(true);
    langNote->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), langNote);

    m_keepClipboard->setChecked(settings::keepClipboardAfterClose());
    form->addRow(QString(), m_keepClipboard);
    auto *clipNote = new QLabel(tr("On Wayland (and on X11 without a clipboard manager) copied images "
                                   "disappear when the application quits. When enabled, Rolidan "
                                   "keeps running in the background without a window until another "
                                   "application takes over the clipboard."),
                                this);
    clipNote->setWordWrap(true);
    form->addRow(QString(), clipNote);

    m_historyLimit->setRange(64, 65536);
    m_historyLimit->setSingleStep(256);
    m_historyLimit->setSuffix(QStringLiteral(" MB"));
    m_historyLimit->setValue(settings::historyLimitMB());
    form->addRow(tr("Undo history memory limit:"), m_historyLimit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(buttons);
}

void SettingsDialog::accept()
{
    const QString lang = m_language->currentData().toString();
    const bool langChanged = lang != settings::language();
    settings::setLanguage(lang);
    settings::setKeepClipboardAfterClose(m_keepClipboard->isChecked());
    settings::setHistoryLimitMB(m_historyLimit->value());
    if (langChanged)
        QMessageBox::information(this, tr("Language"),
                                 tr("The language will change after you restart Rolidan."));
    QDialog::accept();
}
