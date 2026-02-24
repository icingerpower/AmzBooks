#ifndef PANESETTINGSPURCHASEACCOUNT_H
#define PANESETTINGSPURCHASEACCOUNT_H

#include <QWidget>

namespace Ui {
class PaneSettingsPurchaseAccount;
}

class PaneSettingsPurchaseAccount : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsPurchaseAccount(QWidget *parent = nullptr);
    ~PaneSettingsPurchaseAccount();

public slots:
    void addRate();
    void removeRate();

private:
    Ui::PaneSettingsPurchaseAccount *ui;
    void _connectSlots();
};

#endif // PANESETTINGSPURCHASEACCOUNT_H
