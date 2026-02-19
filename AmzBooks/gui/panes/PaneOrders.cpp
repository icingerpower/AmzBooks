#include <QDate>
#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "orders/OrderManager.h"
#include "books/CompanyInfosTable.h"
#include "CurrencyRateManager.h"
#include "orders/OrderTable.h"
#include "orders/TaxAmountTable.h"

#include "PaneOrders.h"
#include "ui_PaneOrders.h"

PaneOrders::PaneOrders(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneOrders)
{
    ui->setupUi(this);
    ui->dateEditMonthlyOrders->setDate(
                QDate::currentDate().addDays(-25));
    _connectSlots();
}

PaneOrders::~PaneOrders()
{
    delete ui;
}

void PaneOrders::displayRecentOrders()
{
    QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
    OrderManager orderManager(workingDir);
    
    CompanyInfosTable companyInfo(workingDir);
    const auto &apiKey = companyInfo.getApiKeyFixer();
    if (apiKey.isEmpty())
    {
        QMessageBox::warning(
                    this,
                    tr("Fixer API key"),
                    tr("Fixer API key is needed for currency rate retrieval"));
        return;
    }
    CurrencyRateManager currencyRateManager(workingDir, apiKey);
    
    QDate dateTo = QDate::currentDate();
    QDate dateFrom = dateTo.addDays(-28);
    
    auto shipmentsMap = orderManager.getShipmentAndRefunds(
                dateFrom, dateTo, [](
                const ActivitySource *source, const Shipment *shipment) {
        return true;
    });
    
    QList<QSharedPointer<Shipment>> shipmentsList;
    for(auto it = shipmentsMap.begin(); it != shipmentsMap.end(); ++it) {
        shipmentsList.append(it.value());
    }
    
    auto orderTable = new OrderTable(shipmentsList, this);
    ui->tableViewOrders->setModel(orderTable);
    
    auto taxTable = new TaxAmountTable(shipmentsList, &currencyRateManager, companyInfo.getCurrency(), companyInfo.getCompanyCountryCode(), this);
    ui->tableViewVat->setModel(taxTable);
}

void PaneOrders::displayMonthlyOrders()
{
    QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
    OrderManager orderManager(workingDir);
    
    CompanyInfosTable companyInfo(workingDir);
    const auto &apiKey = companyInfo.getApiKeyFixer();
    if (apiKey.isEmpty())
    {
        QMessageBox::warning(
                    this,
                    tr("Fixer API key"),
                    tr("Fixer API key is needed for currency rate retrieval"));
        return;
    }
    CurrencyRateManager currencyRateManager(workingDir, apiKey);
    
    QDate dateStart = ui->dateEditMonthlyOrders->date();
    dateStart.setDate(dateStart.year(), dateStart.month(), 1);
    QDate dateEnd = dateStart.addMonths(1).addDays(-1);
    
    auto data = orderManager.get_channel_site_ShipmentAndRefundsConflicts(dateStart, dateEnd);
    
    auto orderTable = new OrderTable(data, this);
    ui->tableViewOrders->setModel(orderTable);
    
    auto taxTable = new TaxAmountTable(data, &currencyRateManager, companyInfo.getCurrency(), companyInfo.getCompanyCountryCode(), this);
    ui->tableViewVat->setModel(taxTable);
}

void PaneOrders::displayOrdersNoInvoices()
{
    QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
    OrderManager orderManager(workingDir);
    
    CompanyInfosTable companyInfo(workingDir);
    const auto &apiKey = companyInfo.getApiKeyFixer();
    if (apiKey.isEmpty())
    {
        QMessageBox::warning(
                    this,
                    tr("Fixer API key"),
                    tr("Fixer API key is needed for currency rate retrieval"));
        return;
    }
    CurrencyRateManager currencyRateManager(workingDir, apiKey);
    
    // Using a broad range or logic from OrderManager
    // OrderManager::getShipmentAndRefundsNoInvoices() takes a date range
    // Assuming we want to check all recent history or a specific range.
    // Let's use start of 2023 or a reasonable default, or maybe just last year?
    // The requirement didn't specify range, but typical usage is broad check.
    QDate dateFrom(2023, 1, 1);
    QDate dateTo = QDate::currentDate();
    
    auto shipmentRefundsList = orderManager.getShipmentAndRefundsNoInvoices(dateFrom, dateTo);
    
    QList<QSharedPointer<Shipment>> shipmentsList;
    if (shipmentRefundsList) {
        for (const auto &item : *shipmentRefundsList) {
            shipmentsList.append(item.shipmentsRefundsSameActivity);
        }
    }
    
    auto orderTable = new OrderTable(shipmentsList, this);
    ui->tableViewOrders->setModel(orderTable);
    
    auto taxTable = new TaxAmountTable(shipmentsList, &currencyRateManager, companyInfo.getCurrency(), companyInfo.getCompanyCountryCode(), this);
    ui->tableViewVat->setModel(taxTable);
}

void PaneOrders::_connectSlots()
{
    connect(ui->buttonDisplayMonthlyOrders,
            &QPushButton::clicked,
            this,
            &PaneOrders::displayMonthlyOrders);
    connect(ui->buttonDisplayRecentOrders,
            &QPushButton::clicked,
            this,
            &PaneOrders::displayRecentOrders);
    connect(ui->buttonDisplayOrdersNoInvoices,
            &QPushButton::clicked,
            this,
            &PaneOrders::displayOrdersNoInvoices);
}
