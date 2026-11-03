#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
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
};
