#include "DialogAddSaleService.h"
#include "ui_DialogAddSaleService.h"
#include "books/ServiceClientManager.h"
#include <QMessageBox>

DialogAddSaleService::DialogAddSaleService(ServiceClientManager *clientManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddSaleService),
    m_clientManager(clientManager)
{
    ui->setupUi(this);
    
    // Setup Client ComboBox
    ui->comboBoxClient->setModel(m_clientManager);
    ui->comboBoxClient->setModelColumn(ServiceClientManager::ColClientName);
    
    ui->dateEdit->setDate(QDate::currentDate());
    
    _setupConnections();
    _updateCurrency(); // Initial update
}

DialogAddSaleService::~DialogAddSaleService()
{
    delete ui;
}

void DialogAddSaleService::_setupConnections()
{
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogAddSaleService::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &DialogAddSaleService::reject);
    
    // Update currency label when client changes
    connect(ui->comboBoxClient, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &DialogAddSaleService::_updateCurrency);
}

void DialogAddSaleService::_updateCurrency()
{
    int row = ui->comboBoxClient->currentIndex();
    if (row >= 0) {
        QString currency = m_clientManager->getCurrency(row);
        ui->labelCurrency->setText(currency);
        
        // Optionally update default amount if unmodified?
        // Let's keep it simple for now. User sets amount.
    }
}

void DialogAddSaleService::setDate(const QDate &date)
{
    ui->dateEdit->setDate(date);
}

void DialogAddSaleService::setAmount(double amount)
{
    ui->doubleSpinBoxAmount->setValue(amount);
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

double DialogAddSaleService::getAmount() const
{
    return ui->doubleSpinBoxAmount->value();
}

QString DialogAddSaleService::getInvoiceId() const
{
    return ui->lineEditReference->text();
}

QString DialogAddSaleService::getCurrency() const
{
    // Return the currency associated with the selected client
    int row = ui->comboBoxClient->currentIndex();
    if (row >= 0) {
        return m_clientManager->getCurrency(row);
    }
    return QString();
}
