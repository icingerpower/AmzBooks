#ifndef DIALOGEDITPURCHASE_H
#define DIALOGEDITPURCHASE_H

#include <QDialog>
#include "books/PurchaseInvoiceManager.h"

namespace Ui {
class DialogEditPurchase;
}

class DialogEditPurchase : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditPurchase(const PurchaseInformation &info,
                                const QString &companyCurrency,
                                QWidget *parent = nullptr);
    ~DialogEditPurchase();

    PurchaseInformation getInfo() const;

private slots:
    void _onAccepted();

private:
    Ui::DialogEditPurchase *ui;
    PurchaseInformation m_info;

    void _setupCurrencies(const QString &companyCurrency, const QString &invoiceCurrency);
};

#endif // DIALOGEDITPURCHASE_H
