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
    ui->comboCountry->addItems(CountriesEu::getCountries());
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
