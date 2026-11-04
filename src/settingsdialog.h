#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    QComboBox *m_language;
    QCheckBox *m_keepClipboard;
    QSpinBox *m_historyLimit;
    QComboBox *m_wheel = nullptr;
    QDoubleSpinBox *m_slideshowSeconds = nullptr;
    QCheckBox *m_slideshowLoop = nullptr;
    QCheckBox *m_confirmTrash = nullptr;
    QCheckBox *m_fullScreenEnlarge = nullptr;
};
