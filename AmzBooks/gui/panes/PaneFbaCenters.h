#ifndef PANEFBACENTERS_H
#define PANEFBACENTERS_H

#include <QWidget>

namespace Ui {
class PaneFbaCenters;
}

class PaneFbaCenters : public QWidget
{
    Q_OBJECT

public:
    explicit PaneFbaCenters(QWidget *parent = nullptr);
    ~PaneFbaCenters();

public slots:
    void addCenter();
    void removeCenter();

private:
    Ui::PaneFbaCenters *ui;
    void _connectSlots();
};

#endif // PANEFBACENTERS_H
