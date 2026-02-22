#include "DialogEditPurchase.h"
#include "ui_DialogEditPurchase.h"

#include <QMessageBox>
#include <QtMath>
#include "CountriesEu.h"

DialogEditPurchase::DialogEditPurchase(const PurchaseInformation &info,
                                       const QString &companyCurrency,
                                       QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditPurchase),
    m_info(info)
{
    ui->setupUi(this);

    ui->dateEdit->setDate(info.date);
    ui->editAccount->setText(info.account);
    ui->editLabel->setText(info.label);
    ui->editSupplier->setText(info.accountSupplier);
    ui->editAmount->setText(info.rawTotalAmount);
    ui->editVatAmount->setText(info.rawVatAmount);
    ui->checkInventory->setChecked(info.isInventory);
    ui->checkDdp->setChecked(info.isDDP);

    _setupCurrencies(companyCurrency, info.currency);
    _setupVatCurrencies(companyCurrency, info.vatCurrency);
    _setupVatCountries(info.vatCountry);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogEditPurchase::_onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogEditPurchase::~DialogEditPurchase()
{
    delete ui;
}

void DialogEditPurchase::_setupCurrencies(const QString &companyCurrency, const QString &invoiceCurrency)
{
    QStringList currencies = CountriesEu::getCurrenciesWorld();

    // Company currency goes first; remove it from its natural position if present
    currencies.removeAll(companyCurrency);
    currencies.prepend(companyCurrency);

    // If the invoice currency is not in the list, append it so it can be selected
    if (!invoiceCurrency.isEmpty() && !currencies.contains(invoiceCurrency)) {
        currencies.append(invoiceCurrency);
    }

    ui->comboCurrency->addItems(currencies);

    // Select the invoice currency, falling back to the company currency
    const QString toSelect = invoiceCurrency.isEmpty() ? companyCurrency : invoiceCurrency;
    const int idx = currencies.indexOf(toSelect);
    if (idx >= 0) {
        ui->comboCurrency->setCurrentIndex(idx);
    }
}

void DialogEditPurchase::_setupVatCurrencies(const QString &companyCurrency, const QString &vatCurrency)
{
    QStringList currencies = CountriesEu::getCurrenciesWorld();

    currencies.removeAll(companyCurrency);
    currencies.prepend(companyCurrency);

    if (!vatCurrency.isEmpty() && !currencies.contains(vatCurrency)) {
        currencies.append(vatCurrency);
    }

    ui->comboVatCurrency->addItems(currencies);

    const QString toSelect = vatCurrency.isEmpty() ? companyCurrency : vatCurrency;
    const int idx = currencies.indexOf(toSelect);
    if (idx >= 0) {
        ui->comboVatCurrency->setCurrentIndex(idx);
    }
}

void DialogEditPurchase::_setupVatCountries(const QString &vatCountry)
{
    // Empty first entry allows "no country" selection
    QStringList countries;
    countries << QString();
    countries << CountriesEu::getCountries();

    ui->comboVatCountry->addItems(countries);

    const int idx = countries.indexOf(vatCountry);
    if (idx >= 0) {
        ui->comboVatCountry->setCurrentIndex(idx);
    }
}

PurchaseInformation DialogEditPurchase::getInfo() const
{
    PurchaseInformation info = m_info;
    info.date = ui->dateEdit->date();
    info.account = ui->editAccount->text().trimmed();
    info.label = ui->editLabel->text().trimmed();
    info.accountSupplier = ui->editSupplier->text().trimmed();
    info.rawTotalAmount = ui->editAmount->text().trimmed();
    info.totalAmount = info.rawTotalAmount.toDouble();
    info.currency = ui->comboCurrency->currentText();
    info.rawVatAmount = ui->editVatAmount->text().trimmed();
    info.vatCurrency = ui->comboVatCurrency->currentText();
    info.vatCountry = ui->comboVatCountry->currentText();
    info.vatTokens.clear();
    info.country_vatRate_vat.clear();
    if (!info.rawVatAmount.isEmpty()) {
        if (!info.vatCountry.isEmpty()) {
            info.vatTokens << QString("%1-TVA-%2%3").arg(info.vatCountry, info.rawVatAmount, info.vatCurrency);
        } else {
            info.vatTokens << QString("TVA-%1%2").arg(info.rawVatAmount, info.vatCurrency);
        }
    }
    info.isInventory = ui->checkInventory->isChecked();
    info.isDDP = ui->checkDdp->isChecked();
    return info;
}

void DialogEditPurchase::_onAccepted()
{
    if (!ui->dateEdit->date().isValid()) {
        QMessageBox::warning(this, tr("Invalid Date"),
                             tr("Please enter a valid date."));
        return;
    }
    if (ui->editAccount->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Account must not be empty."));
        return;
    }
    if (ui->editLabel->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Label must not be empty."));
        return;
    }
    if (ui->editSupplier->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Supplier must not be empty."));
        return;
    }
    if (qAbs(ui->editAmount->text().trimmed().toDouble()) == 0.0) {
        QMessageBox::warning(this, tr("Invalid Amount"),
                             tr("Amount must not be zero."));
        return;
    }
    accept();
}
