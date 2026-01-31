#ifndef PANEPURCHASEACCOUNT_H
#define PANEPURCHASEACCOUNT_H

#include <QWidget>

namespace Ui {
class PanePurchaseAccount;
}

class PanePurchaseAccount : public QWidget
{
    Q_OBJECT

public:
    explicit PanePurchaseAccount(QWidget *parent = nullptr);
    ~PanePurchaseAccount();

public slots:
    void addRate();
    void removeRate();

private:
    Ui::PanePurchaseAccount *ui;
    void _connectSlots();
};

#endif // PANEPURCHASEACCOUNT_H
