#ifndef DIALOGADDSALEACCOUNT_H
#define DIALOGADDSALEACCOUNT_H

#include <QDialog>
#include "books/TaxScheme.h"

namespace Ui {
class DialogAddSaleAccount;
}

class DialogAddSaleAccount : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddSaleAccount(QWidget *parent = nullptr);
    ~DialogAddSaleAccount();

    TaxScheme getTaxScheme() const;
    QString getCountryDeclaring() const;
    QString getCountryFrom() const;
    QString getCountryTo() const;
    double getVatRate() const;
    QString getSaleAccount() const;
    QString getVatAccount() const;
    QString getVatAccountToPay() const;

private:
    Ui::DialogAddSaleAccount *ui;
    void _setupCountries();
    void _setupTaxSchemes();
};

#endif // DIALOGADDSALEACCOUNT_H
