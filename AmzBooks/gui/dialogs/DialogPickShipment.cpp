#include "DialogPickShipment.h"
#include "ui_DialogPickShipment.h"

#include <QMessageBox>

#include "orders/Shipment.h"

DialogPickShipment::DialogPickShipment(const QString &errorTitle,
                                       const QString &errorText,
                                       const QList<QSharedPointer<Shipment>> &shipments,
                                       QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPickShipment)
    , m_shipments(shipments)
{
    ui->setupUi(this);
    setWindowTitle(errorTitle);
    ui->labelErrorDescription->setText(errorText);

    // Populate table
    ui->tableShipments->setColumnCount(5);
    ui->tableShipments->setHorizontalHeaderLabels(
        {tr("Shipment ID"), tr("Amount"), tr("Currency"), tr("From"), tr("To")});
    ui->tableShipments->setRowCount(shipments.size());
    ui->tableShipments->horizontalHeader()->setStretchLastSection(true);
    ui->tableShipments->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    for (int i = 0; i < shipments.size(); ++i) {
        const auto &ship = shipments[i];
        const auto &activities = ship->getActivities();

        double totalTaxed = 0.0;
        QString currency;
        QString countryFrom;
        QString countryTo;

        for (const auto &act : activities) {
            totalTaxed += act.getAmountTaxed();
            if (currency.isEmpty()) {
                currency = act.getCurrency();
            }
            if (countryFrom.isEmpty()) {
                countryFrom = act.getCountryCodeFrom();
            }
            if (countryTo.isEmpty()) {
                countryTo = act.getCountryCodeTo();
            }
        }

        ui->tableShipments->setItem(i, 0, new QTableWidgetItem(ship->getId()));
        ui->tableShipments->setItem(i, 1, new QTableWidgetItem(QString::number(totalTaxed, 'f', 2)));
        ui->tableShipments->setItem(i, 2, new QTableWidgetItem(currency));
        ui->tableShipments->setItem(i, 3, new QTableWidgetItem(countryFrom));
        ui->tableShipments->setItem(i, 4, new QTableWidgetItem(countryTo));
    }

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        int row = ui->tableShipments->currentRow();
        if (row < 0 || row >= m_shipments.size()) {
            QMessageBox::warning(this, tr("Selection Required"),
                                 tr("Please select a shipment before confirming."));
            return;
        }
        m_selectedId = m_shipments[row]->getId();
        accept();
    });
}

DialogPickShipment::~DialogPickShipment()
{
    delete ui;
}

QString DialogPickShipment::selectedShipmentId() const
{
    return m_selectedId;
}
