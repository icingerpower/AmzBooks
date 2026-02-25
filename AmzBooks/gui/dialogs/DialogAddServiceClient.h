#ifndef DIALOGADDSERVICECLIENT_H
#define DIALOGADDSERVICECLIENT_H

#include <QDialog>
#include "books/ServiceClientManager.h"

namespace Ui {
class DialogAddServiceClient;
}

class DialogAddServiceClient : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddServiceClient(QWidget *parent = nullptr);
    ~DialogAddServiceClient();

    QString getClientName() const;
    QString getServiceLabel() const;
    QString getCountry() const;
    QString getVatNumber() const;
    QString getCurrency() const;
    PaymentType getPaymentType() const;
    int getPaymentDays() const;
    QString getStreet1() const;
    QString getStreet2() const;
    QString getPostalCode() const;
    QString getCity() const;
    QString getAccountSale7() const;
    QString getAccountVat() const;
    QString getAccount() const;
    bool getVatOnPayment() const;

private slots:
    void _onPaymentTypeChanged(int index);

private:
    Ui::DialogAddServiceClient *ui;
};

#endif // DIALOGADDSERVICECLIENT_H
