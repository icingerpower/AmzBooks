#include "DialogAddVatRate.h"
#include "ui_DialogAddVatRate.h"
#include "CountriesEu.h"

DialogAddVatRate::DialogAddVatRate(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddVatRate)
{
    ui->setupUi(this);
    _setupCountries();
    _setupSaleTypes();
    ui->dateEdit->setDate(QDate::currentDate());
}

DialogAddVatRate::~DialogAddVatRate()
{
    delete ui;
}

void DialogAddVatRate::_setupCountries()
{
    ui->comboCountry->addItems(CountriesEu::getCountries());
}

void DialogAddVatRate::_setupSaleTypes()
{
    // Populate SaleType combo box manually or via helper if available
    // SaleType enum: Products, Services, ElectronicServices, DigitalServices, Unknown
    ui->comboSaleType->addItem(tr("Products"), QVariant::fromValue(SaleType::Products));
    ui->comboSaleType->addItem(tr("Service"), QVariant::fromValue(SaleType::Service));
    // InventoryMove is internal, typically not for manual VAT rate setting? 
    // But if needed:
    // ui->comboSaleType->addItem(tr("Inventory Move"), QVariant::fromValue(SaleType::InventoryMove));
    // Leaving it out for now as rates are usually for Products/Services.
}

QString DialogAddVatRate::getCountry() const
{
    return ui->comboCountry->currentText();
}

QDate DialogAddVatRate::getDate() const
{
    return ui->dateEdit->date();
}

SaleType DialogAddVatRate::getSaleType() const
{
    return ui->comboSaleType->currentData().value<SaleType>();
}

double DialogAddVatRate::getRate() const
{
    // Spinbox value is in percent (e.g. 20.0), return as decimal (0.20)
    return ui->spinRate->value() / 100.0;
}

QString DialogAddVatRate::getSpecialCode() const
{
    return ui->editSpecialCode->text();
}
