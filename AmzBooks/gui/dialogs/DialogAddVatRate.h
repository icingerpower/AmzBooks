#ifndef DIALOGADDVATRATE_H
#define DIALOGADDVATRATE_H

#include <QDialog>
#include <QDate>
#include "orders/SaleType.h"

namespace Ui {
class DialogAddVatRate;
}

class DialogAddVatRate : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddVatRate(QWidget *parent = nullptr);
    ~DialogAddVatRate();

    QString getCountry() const;
    QDate getDate() const;
    SaleType getSaleType() const;
    double getRate() const;
    QString getSpecialCode() const;

private:
    Ui::DialogAddVatRate *ui;
    void _setupCountries();
    void _setupSaleTypes();
};

#endif // DIALOGADDVATRATE_H
