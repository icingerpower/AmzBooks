#include <QDate>
#include <QMessageBox>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "orders/OrderManager.h"
#include "books/CompanyInfosTable.h"
#include "CurrencyRateManager.h"
#include "orders/OrderTable.h"
#include "orders/OrderCompleteTable.h"
#include "orders/TaxAmountTable.h"
#include "inventory/InventoryMoveTree.h"
#include "CountriesEu.h"

#include "PaneOrders.h"
#include "ui_PaneOrders.h"

PaneOrders::PaneOrders(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneOrders),
    m_companyInfos(nullptr),
    m_currRateManager(nullptr),
    m_inventoryMoveTree(nullptr)
{
    ui->setupUi(this);
    ui->dateEditMonthlyOrders->setDate(
                QDate::currentDate().addDays(-25));
    ui->treeViewInventory->setSortingEnabled(true);

    // Initialise the range pickers to the previous calendar quarter
    {
        const QDate today = QDate::currentDate();
        const int currentQ = (today.month() - 1) / 3;          // 0-based: Q1=0 … Q4=3
        const int prevQ    = (currentQ + 3) % 4;               // wraps Q1 → Q4 of previous year
        const int prevYear = (currentQ == 0) ? today.year() - 1 : today.year();
        const QDate rangeStart(prevYear, prevQ * 3 + 1, 1);
        const QDate rangeEnd = rangeStart.addMonths(3).addDays(-1);
        ui->dateEditStart->setDate(rangeStart);
        ui->dateEditEnd->setDate(rangeEnd);
    }

    _connectSlots();
}

PaneOrders::~PaneOrders()
{
    delete ui;
    // m_companyInfos, m_currRateManager, m_inventoryMoveTree are children of
    // this via the Qt parent mechanism, so they are deleted automatically.
}

void PaneOrders::displayRangeOrders()
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

    const QDate dateStart = ui->dateEditStart->date();
    const QDate dateEnd   = ui->dateEditEnd->date();

    auto data = orderManager.get_channel_site_ShipmentAndRefundsConflicts(dateStart, dateEnd);

    QList<QSharedPointer<Shipment>> allShipments;
    if (data) {
        for (auto itCh = data->constBegin(); itCh != data->constEnd(); ++itCh)
            for (auto itSt = itCh.value().constBegin(); itSt != itCh.value().constEnd(); ++itSt)
                for (auto itCtx = itSt.value().constBegin(); itCtx != itSt.value().constEnd(); ++itCtx)
                    allShipments.append(itCtx.value().shipmentsRefundsSameActivity);
    }
    auto orderIdToSite = orderManager.getStores(allShipments);

    auto orderTable = new OrderCompleteTable(data, orderIdToSite, this);
    ui->tableViewOrders->setModel(orderTable);

    auto taxTable = new TaxAmountTable(data, &currencyRateManager, companyInfo.getCurrency(), companyInfo.getCompanyCountryCode(), this);
    ui->tableViewVat->setModel(taxTable);

    _loadInventoryMoveTree(dateStart, dateEnd);
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
    
    QDate minDateAdded = QDate::currentDate().addDays(-28);

    auto shipmentsMap = orderManager.getShipmentAndRefundsRecentlyAdded(minDateAdded);
    
    QList<QSharedPointer<Shipment>> shipmentsList;
    for(auto it = shipmentsMap.begin(); it != shipmentsMap.end(); ++it) {
        shipmentsList.append(it.value());
    }
    
    auto orderTable = new OrderTable(shipmentsList, this);
    ui->tableViewOrders->setModel(orderTable);

    auto taxTable = new TaxAmountTable(shipmentsList, &currencyRateManager, companyInfo.getCurrency(), companyInfo.getCompanyCountryCode(), this);
    ui->tableViewVat->setModel(taxTable);

    _loadInventoryMoveTree(minDateAdded, QDate::currentDate());
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

    // Collect all shipments from the data to pass to getStores()
    QList<QSharedPointer<Shipment>> allShipments;
    if (data) {
        for (auto itCh = data->constBegin(); itCh != data->constEnd(); ++itCh)
            for (auto itSt = itCh.value().constBegin(); itSt != itCh.value().constEnd(); ++itSt)
                for (auto itCtx = itSt.value().constBegin(); itCtx != itSt.value().constEnd(); ++itCtx)
                    allShipments.append(itCtx.value().shipmentsRefundsSameActivity);
    }
    auto orderIdToSite = orderManager.getStores(allShipments);

    auto orderTable = new OrderCompleteTable(data, orderIdToSite, this);
    ui->tableViewOrders->setModel(orderTable);

    auto taxTable = new TaxAmountTable(data, &currencyRateManager, companyInfo.getCurrency(), companyInfo.getCompanyCountryCode(), this);
    ui->tableViewVat->setModel(taxTable);

    _loadInventoryMoveTree(dateStart, dateEnd);
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

void PaneOrders::filter()
{
    auto *model = ui->tableViewOrders->model();
    if (!model) return;

    const QString text = ui->lineEditFilter->text();

    for (int row = 0; row < model->rowCount(); ++row) {
        const QString orderId     = model->data(model->index(row, OrderCompleteTable::IND_COL_ORDER_ID)).toString();
        const QString activityId  = model->data(model->index(row, OrderCompleteTable::IND_COL_ACTIVITY_ID)).toString();
        const bool match = orderId.contains(text, Qt::CaseInsensitive)
                        || activityId.contains(text, Qt::CaseInsensitive);
        ui->tableViewOrders->setRowHidden(row, !match);
    }
}

void PaneOrders::filterReset()
{
    auto *model = ui->tableViewOrders->model();
    if (!model) return;
    for (int row = 0; row < model->rowCount(); ++row)
        ui->tableViewOrders->setRowHidden(row, false);
}

void PaneOrders::_loadInventoryMoveTree(const QDate &dateStart, const QDate &dateEnd)
{
    QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
    OrderManager orderManager(workingDir);

    // (Re-)create company infos and currency rate manager as parented members so
    // they remain valid for as long as the InventoryMoveTree holds their pointers.
    if (m_companyInfos != nullptr)
    {
        m_companyInfos->deleteLater();
    }
    m_companyInfos = new CompanyInfosTable(workingDir, this);
    if (m_currRateManager != nullptr)
    {
        m_currRateManager->deleteLater();
    }
    m_currRateManager = new CurrencyRateManager(
            workingDir, m_companyInfos->getApiKeyFixer(), this);

    const QStringList &countryCodes = CountriesEu::getAmazonPanEuCountryCodes();

    QHash<QString, QHash<QString, int>> countryCode_sku_unitImported;
    QHash<QString, QHash<QString, int>> countryCode_sku_unitExported;

    // Accumulate inventory moves for every month in [dateStart, dateEnd].
    for (QDate m(dateStart.year(), dateStart.month(), 1); m <= dateEnd; m = m.addMonths(1)) {
        const int year  = m.year();
        const int month = m.month();
        for (const QString &cc : countryCodes) {
            const auto imported = orderManager.getInventoryImported(year, month, cc);
            for (auto it = imported.constBegin(); it != imported.constEnd(); ++it)
                countryCode_sku_unitImported[cc][it.key()] += it.value();

            const auto exported = orderManager.getInventoryExported(year, month, cc);
            for (auto it = exported.constBegin(); it != exported.constEnd(); ++it)
                countryCode_sku_unitExported[cc][it.key()] += it.value();
        }
    }

    // Shipping cost per kg from the purchases widget.
    QHash<QString, double> pricePerKilo;
    for (const QString &cc : countryCodes) {
        pricePerKilo[cc] = ui->widgetPurchases->getShippingPrice(cc);
    }
    pricePerKilo[QString()] = ui->widgetPurchases->getShippingPrice(QString());

    // Replace the old tree (detach model first so the view doesn't keep a
    // dangling pointer while we delete the old instance).
    if (m_inventoryMoveTree) {
        ui->treeViewInventory->setModel(nullptr);
        delete m_inventoryMoveTree;
    }
    m_inventoryMoveTree = new InventoryMoveTree(
            ui->widgetPurchases->getPurchaseDir(),
            countryCode_sku_unitImported,
            countryCode_sku_unitExported,
            pricePerKilo,
            m_companyInfos->getCurrency(),
            m_currRateManager,
            workingDir,
            m_companyInfos->getCompanyCountryCode(),
            this);

    ui->treeViewInventory->setModel(m_inventoryMoveTree);
    for (int col = 0; col < m_inventoryMoveTree->columnCount(); ++col) {
        ui->treeViewInventory->resizeColumnToContents(col);
    }
}

void PaneOrders::_connectSlots()
{
    connect(ui->buttonDisplayRange,
            &QPushButton::clicked,
            this,
            &PaneOrders::displayRangeOrders);
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
    connect(ui->buttonFilter,
            &QPushButton::clicked,
            this,
            &PaneOrders::filter);
    connect(ui->buttonFilterReset,
            &QPushButton::clicked,
            this,
            &PaneOrders::filterReset);
}
