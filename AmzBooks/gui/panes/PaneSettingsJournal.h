#ifndef PANESETTINGSJOURNAL_H
#define PANESETTINGSJOURNAL_H

#include <QWidget>

namespace Ui {
class PaneSettingsJournal;
}

class PaneSettingsJournal : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsJournal(QWidget *parent = nullptr);
    ~PaneSettingsJournal();

private:
    Ui::PaneSettingsJournal *ui;
};

#endif // PANESETTINGSJOURNAL_H
