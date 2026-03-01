#ifndef DIALOGADDSALESERVICE_H
#define DIALOGADDSALESERVICE_H

#include "books/ServiceClientManager.h"
#include <QDialog>
#include <QDate>

namespace Ui {
class DialogAddSaleService;
}

class DialogAddSaleService : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddSaleService(ServiceClientManager *clientManager, QWidget *parent = nullptr);
    ~DialogAddSaleService();

    // Setters for pre-filling
    void setDate(const QDate &date);
    void setUnitPrice(double amount);
    void setReference(const QString &ref);

    // Getters
    QString getSelectedClientName() const;
    QDate getDate() const;
    double getUnitPrice() const;
    int getQuantity() const;
    QString getInvoiceId() const;
    QString getServiceTitle() const;
    QString getCurrency() const;
    QString getAccount() const;
    PaymentType getPaymentType() const;
    int getPaymentDays() const;
    bool getVatOnPayment() const;
    int getSelectedClientRow() const;

private slots:
    void _updateCurrency();
    void _updateOkButton();
    void _updatePaymentDays();

private:
    Ui::DialogAddSaleService *ui;
    ServiceClientManager *m_clientManager;

    void _setupConnections();
};

#endif // DIALOGADDSALESERVICE_H
