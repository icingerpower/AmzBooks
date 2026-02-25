#include "DialogAddServiceClient.h"
#include "ui_DialogAddServiceClient.h"

DialogAddServiceClient::DialogAddServiceClient(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogAddServiceClient)
{
    ui->setupUi(this);
    connect(ui->comboBoxPaymentType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogAddServiceClient::_onPaymentTypeChanged);
}

DialogAddServiceClient::~DialogAddServiceClient()
{
    delete ui;
}

QString DialogAddServiceClient::getClientName() const
{
    return ui->editClientName->text().trimmed();
}

QString DialogAddServiceClient::getServiceLabel() const
{
    return ui->editServiceLabel->text().trimmed();
}

QString DialogAddServiceClient::getCountry() const
{
    return ui->comboBoxCountry->currentText();
}

QString DialogAddServiceClient::getVatNumber() const
{
    return ui->editVatNumber->text().trimmed();
}

QString DialogAddServiceClient::getCurrency() const
{
    return ui->editCurrency->text().trimmed().toUpper();
}

PaymentType DialogAddServiceClient::getPaymentType() const
{
    return static_cast<PaymentType>(ui->comboBoxPaymentType->currentIndex());
}

int DialogAddServiceClient::getPaymentDays() const
{
    return ui->spinBoxPaymentDays->value();
}

QString DialogAddServiceClient::getStreet1() const
{
    return ui->editStreet1->text().trimmed();
}

QString DialogAddServiceClient::getStreet2() const
{
    return ui->editStreet2->text().trimmed();
}

QString DialogAddServiceClient::getPostalCode() const
{
    return ui->editPostalCode->text().trimmed();
}

QString DialogAddServiceClient::getCity() const
{
    return ui->editCity->text().trimmed();
}

QString DialogAddServiceClient::getAccountSale7() const
{
    return ui->editAccountSale7->text().trimmed();
}

QString DialogAddServiceClient::getAccountVat() const
{
    return ui->editAccountVat->text().trimmed();
}

QString DialogAddServiceClient::getAccount() const
{
    return ui->editAccount->text().trimmed();
}

bool DialogAddServiceClient::getVatOnPayment() const
{
    return ui->checkBoxVatOnPayment->isChecked();
}

void DialogAddServiceClient::_onPaymentTypeChanged(int index)
{
    ui->spinBoxPaymentDays->setEnabled(index == static_cast<int>(PaymentType::AfterXDays));
}
