#include "DialogAddSaleAccount.h"
#include "ui_DialogAddSaleAccount.h"
#include "CountriesEu.h"
#include "orders/SaleType.h"

DialogAddSaleAccount::DialogAddSaleAccount(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddSaleAccount)
{
    ui->setupUi(this);
    _setupCountries();
    _setupTaxSchemes();
    _setupSaleTypes();
}

DialogAddSaleAccount::~DialogAddSaleAccount()
{
    delete ui;
}

void DialogAddSaleAccount::_setupCountries()
{
    const QStringList countries = CountriesEu::getCountries();
    ui->comboCountryDeclaring->addItems(countries);
    ui->comboCountryFrom->addItems(countries);
    ui->comboCountryTo->addItems(countries);
    
    // Default to FR for convenience
    int index = ui->comboCountryDeclaring->findText("FR");
    if (index != -1) {
        ui->comboCountryDeclaring->setCurrentIndex(index);
        ui->comboCountryFrom->setCurrentIndex(index);
        ui->comboCountryTo->setCurrentIndex(index);
    }
}

void DialogAddSaleAccount::_setupTaxSchemes()
{
    // Populate relevant schemes
    // Using QVariant to store Enum value
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::DomesticVat), QVariant::fromValue(TaxScheme::DomesticVat));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::EuOssUnion), QVariant::fromValue(TaxScheme::EuOssUnion));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::EuOssNonUnion), QVariant::fromValue(TaxScheme::EuOssNonUnion));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::EuIoss), QVariant::fromValue(TaxScheme::EuIoss));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::ImportVat), QVariant::fromValue(TaxScheme::ImportVat));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::ReverseChargeImport), QVariant::fromValue(TaxScheme::ReverseChargeImport));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::ReverseChargeDomestic), QVariant::fromValue(TaxScheme::ReverseChargeDomestic));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::MarketplaceDeemedSupplier), QVariant::fromValue(TaxScheme::MarketplaceDeemedSupplier));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::Exempt), QVariant::fromValue(TaxScheme::Exempt));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::OutOfScope), QVariant::fromValue(TaxScheme::OutOfScope));
    ui->comboTaxScheme->addItem(taxSchemeToString(TaxScheme::Unknown), QVariant::fromValue(TaxScheme::Unknown));
}

TaxScheme DialogAddSaleAccount::getTaxScheme() const
{
    return ui->comboTaxScheme->currentData().value<TaxScheme>();
}

QString DialogAddSaleAccount::getCountryDeclaring() const
{
    return ui->comboCountryDeclaring->currentText();
}

QString DialogAddSaleAccount::getCountryFrom() const
{
    return ui->comboCountryFrom->currentText();
}

QString DialogAddSaleAccount::getCountryTo() const
{
    return ui->comboCountryTo->currentText();
}

double DialogAddSaleAccount::getVatRate() const
{
    return ui->spinVatRate->value();
}

QString DialogAddSaleAccount::getSaleAccount() const
{
    return ui->editSaleAccount->text();
}

QString DialogAddSaleAccount::getVatAccount() const
{
    return ui->editVatAccount->text();
}

QString DialogAddSaleAccount::getVatAccountToPay() const
{
    return ui->editVatAccountToPay->text();
}

void DialogAddSaleAccount::_setupSaleTypes()
{
    ui->comboSaleType->addItem(toString(SaleType::Products), QVariant::fromValue(SaleType::Products));
    ui->comboSaleType->addItem(toString(SaleType::Service),  QVariant::fromValue(SaleType::Service));
}

SaleType DialogAddSaleAccount::getSaleType() const
{
    return ui->comboSaleType->currentData().value<SaleType>();
}

QString DialogAddSaleAccount::getClientAccount() const
{
    return ui->editClientAccount->text();
}
