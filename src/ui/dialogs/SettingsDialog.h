#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QKeySequenceEdit;
class GlossaryTable;
class GlobalHotkey;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(GlobalHotkey* hotkey, int initialTab = 0, QWidget* parent = nullptr);

protected:
    void reject() override;

private:
    QWidget* createApiPage();
    QWidget* createTranslationPage();
    QWidget* createGlossaryPage();
    QWidget* createPromptsPage();
    QWidget* createInterfacePage();
    QWidget* createHotkeyPage();
    QWidget* createClipboardPage();
    QWidget* createHistoryPage();

    QJsonObject m_snapshot;
};
