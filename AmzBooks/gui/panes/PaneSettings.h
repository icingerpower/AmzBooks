#ifndef PANESETTINGS_H
#define PANESETTINGS_H

#include <QWidget>

namespace Ui {
class PaneSettings;
}

class PaneSettings : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettings(QWidget *parent = nullptr);
    ~PaneSettings();

private:
    Ui::PaneSettings *ui;
};

#endif // PANESETTINGS_H
