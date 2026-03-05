#include "DialogViewShipments.h"
#include "ui_DialogViewShipments.h"
#include "orders/Refund.h"
#include "books/TaxScheme.h"
#include <QStandardItemModel>
#include <QHeaderView>

DialogViewShipments::DialogViewShipments(const QList<OrderManager::ShipmentRefundsWithUpdates> &entries,
                                         int year,
                                         const OrderManager *orderManager,
                                         QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogViewShipments)
{
    ui->setupUi(this);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({
        tr("Store"),
        tr("Date"),
        tr("Order ID"),
        tr("Shipment/Refund ID"),
        tr("Taxed Amount"),
        tr("VAT Amount"),
        tr("VAT Scheme"),
        tr("Type")
    });

    QList<QSharedPointer<Shipment>> allShipments;
    for (const auto &entry : entries) {
        for (const auto &shipment : entry.shipmentsRefundsSameActivity) {
            allShipments.append(shipment);
        }
    }
    QHash<QString, QString> stores = orderManager->getStores(allShipments);

    int count = 0;
    for (const auto &entry : entries) {
        const auto &shipments = entry.shipmentsRefundsSameActivity;
        for (int i = 0; i < shipments.size(); ++i) {
            if (i >= entry.invoicesToDo.size() || !entry.invoicesToDo[i])
                continue;

            const auto &shipment = shipments[i];
            const auto &activities = shipment->getActivities();
            if (activities.isEmpty())
                continue;

            const bool isRefund = dynamic_cast<const Refund *>(shipment.data()) != nullptr;
            const QString orderId = activities.first().getEventId();
            const QString shipmentId = shipment->getId();
            const TaxScheme scheme = activities.first().getTaxScheme();
            const QString dateStr = activities.first().getDateTime().date().toString(Qt::ISODate);
            const QString store = stores.value(orderId);

            double totalTaxed = 0.0;
            double totalVat = 0.0;
            for (const auto &act : activities) {
                totalTaxed += act.getAmountTaxed();
                totalVat   += act.getAmountTaxes();
            }

            auto makeItem = [](const QString &text) {
                auto *item = new QStandardItem(text);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
                return item;
            };

            QList<QStandardItem *> row;
            row << makeItem(store);
            row << makeItem(dateStr);
            row << makeItem(orderId);
            row << makeItem(shipmentId);
            row << makeItem(QString::number(totalTaxed, 'f', 2));
            row << makeItem(QString::number(totalVat, 'f', 2));
            row << makeItem(taxSchemeToString(scheme));
            row << makeItem(isRefund ? tr("Refund") : tr("Order"));
            m_model->appendRow(row);
            ++count;
        }
    }

    ui->labelInfo->setText(
        tr("%1 order(s) in %2 do not have invoices yet. Do you want to generate them now?")
            .arg(count).arg(year));

    ui->tableView->setModel(m_model);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setStretchLastSection(true);
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->tableView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
}

DialogViewShipments::~DialogViewShipments()
{
    delete ui;
}
