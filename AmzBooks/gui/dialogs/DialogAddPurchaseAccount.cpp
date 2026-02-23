#include "DialogAddPurchaseAccount.h"
#include "ui_DialogAddPurchaseAccount.h"
#include "CountriesEu.h"

DialogAddPurchaseAccount::DialogAddPurchaseAccount(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddPurchaseAccount)
{
    ui->setupUi(this);
    _setupCountries();
}

DialogAddPurchaseAccount::~DialogAddPurchaseAccount()
{
    delete ui;
}

void DialogAddPurchaseAccount::_setupCountries()
{
    QStringList countries = CountriesEu::getCountries();
    // Add an empty country representation for "Any" or wildcard
    countries.prepend("");
    
    ui->comboCountry->addItems(countries);
    
    // Select FR by default if available
    int index = ui->comboCountry->findText("FR");
    if (index != -1) {
        ui->comboCountry->setCurrentIndex(index);
    }
}

QString DialogAddPurchaseAccount::getCountry() const
{
    return ui->comboCountry->currentText();
}

double DialogAddPurchaseAccount::getVatRate() const
{
    return ui->spinVatRate->value();
}

QString DialogAddPurchaseAccount::getAccountDebit6() const
{
    return ui->editAccountDebit6->text();
}

QString DialogAddPurchaseAccount::getAccountCredit4() const
{
    return ui->editAccountCredit4->text();
}
