#ifndef DIALOGADDPURCHASEACCOUNT_H
#define DIALOGADDPURCHASEACCOUNT_H

#include <QDialog>

namespace Ui {
class DialogAddPurchaseAccount;
}

class DialogAddPurchaseAccount : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddPurchaseAccount(QWidget *parent = nullptr);
    ~DialogAddPurchaseAccount();

    QString getCountry() const;
    double getVatRate() const;
    QString getAccountDebit6() const;
    QString getAccountCredit4() const;

private:
    Ui::DialogAddPurchaseAccount *ui;
    void _setupCountries();
};

#endif // DIALOGADDPURCHASEACCOUNT_H
