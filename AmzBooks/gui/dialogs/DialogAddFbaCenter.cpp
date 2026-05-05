#include "DialogAddFbaCenter.h"
#include "ui_DialogAddFbaCenter.h"
#include "CountriesEu.h"

DialogAddFbaCenter::DialogAddFbaCenter(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddFbaCenter)
{
    ui->setupUi(this);
    _setupCountries();
}

DialogAddFbaCenter::~DialogAddFbaCenter()
{
    delete ui;
}

void DialogAddFbaCenter::_setupCountries()
{
    // Non-EU countries first (per CLAUDE.md UI standard), then EU countries
    QStringList countries;
    countries << "US" << "CA" << "CN" << "JP" << "AU" << "MX" << "TR" << "IN" << "BR" << "SA" << "AE" << "SG";
    countries << CountriesEu::getCountries();
    ui->comboCountry->addItems(countries);
}

QString DialogAddFbaCenter::getCenterId() const
{
    return ui->editCenterId->text();
}

QString DialogAddFbaCenter::getCountryCode() const
{
    return ui->comboCountry->currentText();
}

QString DialogAddFbaCenter::getPostalCode() const
{
    return ui->editPostalCode->text();
}

QString DialogAddFbaCenter::getCity() const
{
    return ui->editCity->text();
}
