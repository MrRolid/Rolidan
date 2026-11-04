#include "settingsdialog.h"

#include "i18n.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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

    // Viewer (Etapa 5)
    m_wheel = new QComboBox(this);
    m_wheel->setObjectName(QStringLiteral("wheel"));
    m_wheel->addItem(tr("Scrolls the image"), int(settings::Wheel::Scroll));
    m_wheel->addItem(tr("Shows the next / previous image"), int(settings::Wheel::Browse));
    m_wheel->addItem(tr("Zooms"), int(settings::Wheel::Zoom));
    m_wheel->setCurrentIndex(m_wheel->findData(int(settings::wheelAction())));
    m_wheel->setToolTip(tr("Ctrl + wheel always zooms"));
    form->addRow(tr("Mouse wheel:"), m_wheel);

    m_fullScreenEnlarge = new QCheckBox(tr("In full screen and slideshow, enlarge small images to fit the screen (Z)"), this);
    m_fullScreenEnlarge->setObjectName(QStringLiteral("fullScreenEnlarge"));
    m_fullScreenEnlarge->setChecked(settings::fullScreenEnlarge());
    form->addRow(tr("Full screen:"), m_fullScreenEnlarge);

    m_slideshowSeconds = new QDoubleSpinBox(this);
    m_slideshowSeconds->setObjectName(QStringLiteral("slideshowSeconds"));
    m_slideshowSeconds->setRange(0.5, 3600);
    m_slideshowSeconds->setDecimals(1);
    m_slideshowSeconds->setSingleStep(0.5);
    m_slideshowSeconds->setSuffix(tr(" s"));
    m_slideshowSeconds->setValue(settings::slideshowSeconds());
    form->addRow(tr("Slideshow interval:"), m_slideshowSeconds);
    m_slideshowLoop = new QCheckBox(tr("Start again after the last image"), this);
    m_slideshowLoop->setChecked(settings::slideshowLoop());
    form->addRow(QString(), m_slideshowLoop);

    m_confirmTrash = new QCheckBox(tr("Ask before moving a file to the trash"), this);
    m_confirmTrash->setChecked(settings::confirmTrash());
    form->addRow(QString(), m_confirmTrash);

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
    settings::setWheelAction(settings::Wheel(m_wheel->currentData().toInt()));
    settings::setSlideshowSeconds(m_slideshowSeconds->value());
    settings::setSlideshowLoop(m_slideshowLoop->isChecked());
    settings::setConfirmTrash(m_confirmTrash->isChecked());
    settings::setFullScreenEnlarge(m_fullScreenEnlarge->isChecked());
    if (langChanged)
        QMessageBox::information(this, tr("Language"),
                                 tr("The language will change after you restart Rolidan."));
    QDialog::accept();
}
