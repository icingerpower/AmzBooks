#ifndef PANEORDERS_H
#define PANEORDERS_H

#include <QWidget>

namespace Ui {
class PaneOrders;
}

class PaneOrders : public QWidget
{
    Q_OBJECT

public:
    explicit PaneOrders(QWidget *parent = nullptr);
    ~PaneOrders();

public slots:
    void displayRecentOrders();
    void displayMonthlyOrders();
    void displayOrdersNoInvoices();

private:
    Ui::PaneOrders *ui;
    void _connectSlots();
};

#endif // PANEORDERS_H
