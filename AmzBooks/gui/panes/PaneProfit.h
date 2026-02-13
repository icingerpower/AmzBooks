#ifndef PANEPROFIT_H
#define PANEPROFIT_H

#include <QWidget>

namespace Ui {
class PaneProfit;
}

class PaneProfit : public QWidget
{
    Q_OBJECT

public:
    explicit PaneProfit(QWidget *parent = nullptr);
    ~PaneProfit();

public slots:
    void browseEconomicFolder();

private:
    Ui::PaneProfit *ui;
    static const QString SETTINGS_KEY_ECONOMIC_FOLDER;
    void _connectSlots();
};

#endif // PANEPROFIT_H
