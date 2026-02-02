#ifndef PANESETTINGSFBACENTERS_H
#define PANESETTINGSFBACENTERS_H

#include <QWidget>

namespace Ui {
class PaneSettingsFbaCenters;
}

class PaneSettingsFbaCenters : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsFbaCenters(QWidget *parent = nullptr);
    ~PaneSettingsFbaCenters();

public slots:
    void addCenter();
    void removeCenter();

private:
    Ui::PaneSettingsFbaCenters *ui;
    void _connectSlots();
};

#endif // PANESETTINGSFBACENTERS_H
