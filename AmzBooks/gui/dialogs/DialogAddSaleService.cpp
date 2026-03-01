#include "DialogAddSaleService.h"
#include "ui_DialogAddSaleService.h"
#include <QDialogButtonBox>
#include <QPushButton>

DialogAddSaleService::DialogAddSaleService(ServiceClientManager *clientManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddSaleService),
    m_clientManager(clientManager)
{
    ui->setupUi(this);

    ui->comboBoxClient->setModel(m_clientManager);
    ui->comboBoxClient->setModelColumn(ServiceClientManager::ColClientName);

    // Populate payment term combo from the canonical labels defined in ServiceClientManager
    ui->comboBoxPaymentTerm->addItems(ServiceClientManager::paymentTypeLabels());
    ui->comboBoxPaymentTerm->setCurrentIndex(static_cast<int>(PaymentType::EndOfNextMonth));

    ui->dateEdit->setDate(QDate::currentDate());

    // OK starts disabled — requires valid input
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    _setupConnections();
    _updateCurrency();
}

DialogAddSaleService::~DialogAddSaleService()
{
    delete ui;
}

void DialogAddSaleService::_setupConnections()
{
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogAddSaleService::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &DialogAddSaleService::reject);

    connect(ui->comboBoxClient, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogAddSaleService::_updateCurrency);

    connect(ui->lineEditReference,    &QLineEdit::textChanged,
            this, &DialogAddSaleService::_updateOkButton);
    connect(ui->lineEditServiceTitle, &QLineEdit::textChanged,
            this, &DialogAddSaleService::_updateOkButton);
    connect(ui->doubleSpinBoxUnitPrice, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DialogAddSaleService::_updateOkButton);
    connect(ui->spinBoxQuantity, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DialogAddSaleService::_updateOkButton);

    connect(ui->comboBoxPaymentTerm, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogAddSaleService::_updatePaymentDays);
}

void DialogAddSaleService::_updatePaymentDays()
{
    bool afterXDays = (ui->comboBoxPaymentTerm->currentIndex() == static_cast<int>(PaymentType::AfterXDays));
    ui->spinBoxPaymentDays->setEnabled(afterXDays);
}

void DialogAddSaleService::_updateCurrency()
{
    int row = ui->comboBoxClient->currentIndex();
    if (row >= 0)
        ui->labelCurrency->setText(m_clientManager->getCurrency(row));
}

void DialogAddSaleService::_updateOkButton()
{
    bool valid = ui->doubleSpinBoxUnitPrice->value() > 0.0
              && ui->spinBoxQuantity->value() > 0
              && !ui->lineEditReference->text().trimmed().isEmpty()
              && !ui->lineEditServiceTitle->text().trimmed().isEmpty();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

void DialogAddSaleService::setDate(const QDate &date)
{
    ui->dateEdit->setDate(date);
}

void DialogAddSaleService::setUnitPrice(double amount)
{
    ui->doubleSpinBoxUnitPrice->setValue(amount);
}

void DialogAddSaleService::setReference(const QString &ref)
{
    ui->lineEditReference->setText(ref);
}

QString DialogAddSaleService::getSelectedClientName() const
{
    return ui->comboBoxClient->currentText();
}

int DialogAddSaleService::getSelectedClientRow() const
{
    return ui->comboBoxClient->currentIndex();
}

QDate DialogAddSaleService::getDate() const
{
    return ui->dateEdit->date();
}

double DialogAddSaleService::getUnitPrice() const
{
    return ui->doubleSpinBoxUnitPrice->value();
}

int DialogAddSaleService::getQuantity() const
{
    return ui->spinBoxQuantity->value();
}

QString DialogAddSaleService::getInvoiceId() const
{
    return ui->lineEditReference->text();
}

QString DialogAddSaleService::getServiceTitle() const
{
    return ui->lineEditServiceTitle->text();
}

QString DialogAddSaleService::getCurrency() const
{
    int row = ui->comboBoxClient->currentIndex();
    if (row >= 0)
        return m_clientManager->getCurrency(row);
    return QString();
}

QString DialogAddSaleService::getAccount() const
{
    int row = ui->comboBoxClient->currentIndex();
    if (row >= 0)
        return m_clientManager->getAccount(row);
    return QString();
}

PaymentType DialogAddSaleService::getPaymentType() const
{
    return static_cast<PaymentType>(ui->comboBoxPaymentTerm->currentIndex());
}

int DialogAddSaleService::getPaymentDays() const
{
    return ui->spinBoxPaymentDays->value();
}

bool DialogAddSaleService::getVatOnPayment() const
{
    return ui->checkBoxVatOnPayment->isChecked();
}
