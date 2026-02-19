#include "DialogViewOrders.h"
#include "ui_DialogViewOrders.h"
#include "orders/OrderTable.h"
#include "orders/TaxAmountTable.h"
#include "orders/OrderAddressTable.h"
#include "orders/OrderInvoicingTable.h"
#include "orders/Shipment.h"
#include "orders/Refund.h"
#include <QStandardItemModel>

DialogViewOrders::DialogViewOrders(const AbstractImporter::OrderInfos &orderInfos
                                   , const CurrencyRateManager *currencyRateManager
                                   , const QString &destCurrency
                                   , QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogViewOrders)
{
    ui->setupUi(this);

    // Set Dates
    ui->dateEditMin->setDate(orderInfos.dateMin);
    ui->dateEditMax->setDate(orderInfos.dateMax);

    // Prepare data for Order and Tax tables
    QList<QSharedPointer<Shipment>> shipments;
    shipments.reserve(orderInfos.shipments.size() + orderInfos.refunds.size());

    for (const auto &s : orderInfos.shipments) {
        shipments.append(QSharedPointer<Shipment>::create(s));
    }
    for (const auto &r : orderInfos.refunds) {
        shipments.append(QSharedPointer<Refund>::create(r));
    }

    // Setup Order Table
    m_orderTable = new OrderTable(shipments, this);
    ui->tableViewOrders->setModel(m_orderTable);
    ui->tableViewOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Setup Tax Table
    m_taxAmountTable = new TaxAmountTable(shipments, currencyRateManager, destCurrency, QString(), this);
    ui->tableViewTaxes->setModel(m_taxAmountTable);
    ui->tableViewTaxes->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Setup Address Table
    m_addressTable = new OrderAddressTable(orderInfos.orderAddresses, this);
    ui->tableViewAddresses->setModel(m_addressTable);
    ui->tableViewAddresses->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Setup Invoicing Table
    m_invoicingTable = new OrderInvoicingTable(orderInfos.invoicingInfos, this);
    ui->tableViewInvoicing->setModel(m_invoicingTable);
    ui->tableViewInvoicing->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Setup Refund Clues Table
    m_refundClueModel = new QStandardItemModel(this);
    m_refundClueModel->setColumnCount(3);
    m_refundClueModel->setHorizontalHeaderLabels({tr("Order ID"), tr("Amount"), tr("Currency")});
    for (auto it = orderInfos.orderId_refundClue.constBegin(); it != orderInfos.orderId_refundClue.constEnd(); ++it) {
        QList<QStandardItem *> row;
        row << new QStandardItem(it.key());
        row << new QStandardItem(QString::number(it.value().value, 'f', 2));
        row << new QStandardItem(it.value().currency);
        m_refundClueModel->appendRow(row);
    }
    ui->tableViewRefundClues->setModel(m_refundClueModel);
    ui->tableViewRefundClues->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // Setup Store Info Table
    m_storeInfoModel = new QStandardItemModel(this);
    m_storeInfoModel->setColumnCount(2);
    m_storeInfoModel->setHorizontalHeaderLabels({tr("Event ID"), tr("Store")});
    for (auto it = orderInfos.orderId_store.constBegin(); it != orderInfos.orderId_store.constEnd(); ++it) {
        QList<QStandardItem *> row;
        row << new QStandardItem(it.key());
        row << new QStandardItem(it.value());
        m_storeInfoModel->appendRow(row);
    }
    ui->tableViewStoreInfos->setModel(m_storeInfoModel);
    ui->tableViewStoreInfos->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    // Connect buttons
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogViewOrders::~DialogViewOrders()
{
    delete ui;
}
