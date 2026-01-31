#include <CountriesEu.h>

#include "DialogAddVatNumber.h"
#include "ui_DialogAddVatNumber.h"

#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

DialogAddVatNumber::DialogAddVatNumber(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddVatNumber)
{
    ui->setupUi(this);
    
    _populateCountries();
    
    connect(ui->comboCountry, SIGNAL(currentIndexChanged(int)), this, SLOT(onCountryChanged(int)));
    connect(ui->editVatNumber, &QLineEdit::textChanged, this, &DialogAddVatNumber::validateInput);
    
    // Initial validation
    validateInput();
}

DialogAddVatNumber::~DialogAddVatNumber()
{
    delete ui;
}

QString DialogAddVatNumber::getSelectedCountry() const
{
    return ui->comboCountry->currentText();
}

QString DialogAddVatNumber::getVatNumber() const
{
    return ui->editVatNumber->text();
}

void DialogAddVatNumber::_populateCountries()
{
    // EU Countries + GB
    QStringList countries = CountriesEu::getCountries();
    ui->comboCountry->addItems(countries);
    
    // Default to FR or first
    int frIndex = ui->comboCountry->findText("FR");
    if (frIndex != -1) {
        ui->comboCountry->setCurrentIndex(frIndex);
    }
}

void DialogAddVatNumber::onCountryChanged(int index)
{
    Q_UNUSED(index);
    // Here we could update validator based on country if we had specific rules per country
    // For now, let's reset or just re-validate
    validateInput();
}

void DialogAddVatNumber::validateInput()
{
    bool isValid = true;
    QString vat = ui->editVatNumber->text().trimmed();
    
    if (vat.isEmpty()) {
        isValid = false;
    } else {
        // Generic regex for VAT: CountryCode (2 letters) + alphanumeric
        // Or simplified: just ensure it starts with Country Code? 
        // Request said: checking the validity with qt regex
        
        QString country = getSelectedCountry();
        // Typically VAT number starts with Country Code (except maybe UK sometimes but usually GB...)
        
        // Let's enforce it starts with Country Code
        if (!vat.startsWith(country, Qt::CaseInsensitive)) {
            // Or maybe we don't force it, but usually it does. 
            // Let's just check alphanumeric and min length
             QRegularExpression regex("^[A-Z]{2}[A-Z0-9+=]{2,12}$"); // Generic
             if (!regex.match(vat).hasMatch()) {
                 isValid = false;
             }
        }
    }
    
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(isValid);
}
