#include "DialogViewOrders.h"
#include "ui_DialogViewOrders.h"
#include "orders/OrderTable.h"
#include "orders/TaxAmountTable.h"
#include "orders/OrderAddressTable.h"
#include "orders/OrderInvoicingTable.h"
#include "orders/Shipment.h"
#include "orders/Refund.h"

DialogViewOrders::DialogViewOrders(const AbstractImporter::OrderInfos &orderInfos, const CurrencyRateManager *currencyRateManager, const QString &destCurrency, QWidget *parent) :
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
    m_taxAmountTable = new TaxAmountTable(shipments, currencyRateManager, destCurrency, this);
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
    
    // Connect buttons
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogViewOrders::~DialogViewOrders()
{
    delete ui;
}
