#include "PaneOrderApis.h"
#include "ui_PaneOrderApis.h"

#include <QMessageBox>
#include <QDebug>
#include <QCoroTask>

#include <QException>

#include "../../../../common/workingdirectory/WorkingDirectoryManager.h"

#include "orders/ApiImportersTable.h"
#include "orders/ParamsTable.h"
#include "orders/AbstractImporterApi.h"
#include "orders/OrderManager.h"
#include "books/ActivityTable.h"

PaneOrderApis::PaneOrderApis(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneOrderApis),
    m_importersTable(new ApiImportersTable(this))
{
    ui->setupUi(this);

    // Setup Importers Table
    ui->tableImporters->setModel(m_importersTable);
    ui->tableImporters->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableImporters->horizontalHeader()->setStretchLastSection(true);
    ui->tableImporters->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableImporters->setSelectionMode(QAbstractItemView::SingleSelection);

    // Setup Params Table
    ui->tableParams->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableParams->horizontalHeader()->setStretchLastSection(true);

    // Setup Orders Table
    ui->tableOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableOrders->horizontalHeader()->setStretchLastSection(true);
    ui->tableOrders->setSortingEnabled(true);

    _connectSlots();
    
    // Select first if available
    if (m_importersTable->rowCount(QModelIndex()) > 0) {
        ui->tableImporters->setCurrentIndex(m_importersTable->index(0, 0));
    }
}

PaneOrderApis::~PaneOrderApis()
{
    delete ui;
}

void PaneOrderApis::_connectSlots()
{
    connect(ui->buttonImport, &QPushButton::clicked, this, &PaneOrderApis::import);

    connect(ui->tableImporters->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            &PaneOrderApis::onImporterSelected);
}

void PaneOrderApis::onImporterSelected(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid()) {
        ui->tableParams->setModel(nullptr);
        if (m_paramsModel) {
            delete m_paramsModel;
            m_paramsModel = nullptr;
        }
        return;
    }

    AbstractImporterApi *importer = m_importersTable->getImporter(current);
    if (!importer) {
        ui->tableParams->setModel(nullptr);
        return;
    }

    // Create new ParamsTable
    auto *oldModel = m_paramsModel;
    m_paramsModel = new ParamsTable(importer, this);
    ui->tableParams->setModel(m_paramsModel);
    ui->tableParams->setVisible(m_paramsModel->rowCount() > 0);

    // Setup Activity Table for Importer
    if (!m_activityModels.contains(importer->getId())) {
        m_activityModels[importer->getId()] = new ActivityTable(this);
    }
    ui->tableOrders->setModel(m_activityModels[importer->getId()]);

    if (oldModel) {
        oldModel->deleteLater();
    }
    
    updateChart();

    // Default dates? AbstractImporterApi doesn't seemingly expose a cached "datesFromTo" 
    // for all types generally like file importer, but specific methods. 
    // We can just default to today or something appropriate.
    ui->dateEditMin->setDate(QDate::currentDate().addMonths(-1));
    ui->dateEditMax->setDate(QDate::currentDate());
}

void PaneOrderApis::import()
{
    QModelIndex index = ui->tableImporters->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Import"), tr("Please select an importer first."));
        return;
    }

    AbstractImporterApi *importer = m_importersTable->getImporter(index);
    if (!importer) return;

    QDateTime dateFrom = ui->dateEditMin->dateTime();
    // API logic implies fetching FROM a date.

    // Run asynchronously
    [](PaneOrderApis *self, AbstractImporterApi *importer, QDateTime from) -> QCoro::Task<void> {
        self->ui->buttonImport->setEnabled(false);

        try {
            // Fetch Shipments
            auto resultShipments = co_await importer->fetchShipments(from);
            // Fetch Refunds
            auto resultRefunds = co_await importer->fetchRefunds(from);
            // Fetch Addresses
            auto resultAddresses = co_await importer->fetchAddresses(from);
            // Fetch Invoicing Infos
            auto resultInvoiceInfos = co_await importer->fetchInvoiceInfos(from);

            // Combine results for processing?
            // Or process each. OrderManager handles recording.
            
            OrderManager manager(WorkingDirectoryManager::instance()->workingDir());
            ActivitySource source = importer->getActivitySource(); // Assuming Api has this? Yes AbstractImporter has it.

            int importedCount = 0;
            QList<Activity> newActivities;

            // Process Shipments
            if (resultShipments.orderInfos) {
                 for (const auto &shipment : resultShipments.orderInfos->shipments) {
                     manager.recordShipmentFromSource(shipment.getId(), &source, &shipment, QDate());
                     newActivities.append(shipment.getActivities());
                 }
                 importedCount += resultShipments.orderInfos->shipments.size();
            }

            // Process Refunds
            if (resultRefunds.orderInfos) {
                for (const auto &refund : resultRefunds.orderInfos->refunds) {
                    manager.recordShipmentFromSource(refund.getId(), &source, &refund, QDate());
                    newActivities.append(refund.getActivities());
                }
                importedCount += resultRefunds.orderInfos->refunds.size();
            }
            // Process Addresses
             if (resultAddresses.orderInfos) {
                for (const auto &addr : resultAddresses.orderInfos->orderAddresses) {
                    manager.recordAddressTo(addr.orderId, addr.address);
                }
            }
            // Process Invoicing Infos
             if (resultInvoiceInfos.orderInfos) {
                for (const auto &inv : resultInvoiceInfos.orderInfos->invoicingInfos) {
                    manager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
                }
            }

            // Update Chart & Table
            if (importedCount > 0) {
                 auto &counts = self->m_ordersData[importer->getId()];
                 for (const auto &activity : newActivities) {
                     counts[activity.getDateTime().date()]++;
                 }

                 QMetaObject::invokeMethod(self, "updateChart", Qt::QueuedConnection);
                 
                 QMetaObject::invokeMethod(self, [self, importerId = importer->getId(), newActivities]() {
                    if (self->m_activityModels.contains(importerId)) {
                        self->m_activityModels[importerId]->addActivities(newActivities);
                    }
                 }, Qt::QueuedConnection);
            }

            QMessageBox::information(self, tr("Import Successful"), 
                                     tr("Successfully fetched data."));

        } catch (const QException &e) {
             QMessageBox::critical(self, tr("Import Error"), e.what());
        } catch (const std::exception &e) {
             QMessageBox::critical(self, tr("Import Error"), QString(e.what()));
        }

        self->ui->buttonImport->setEnabled(true);
    }(this, importer, dateFrom);
}

void PaneOrderApis::updateChart()
{
    // Clear previous chart
    ui->chartOrders->setChart(new QChart());
    
    QModelIndex index = ui->tableImporters->currentIndex();
    if (!index.isValid()) return;
    
    AbstractImporterApi *importer = m_importersTable->getImporter(index);
    if (!importer) return;
    
    const auto &counts = m_ordersData[importer->getId()];
    if (counts.isEmpty()) return;
    
    auto *series = new QLineSeries();
    series->setName(tr("Orders"));
    
    int maxCount = 0;
    
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        series->append(it.key().startOfDay().toMSecsSinceEpoch(), it.value());
        if (it.value() > maxCount) maxCount = it.value();
    }
    
    auto *chart = new QChart();
    chart->addSeries(series);
    chart->legend()->hide();
    chart->setTitle(tr("Imported Orders Over Time"));
    
    auto *axisX = new QDateTimeAxis();
    axisX->setTickCount(10);
    axisX->setFormat("dd MMM yyyy");
    axisX->setTitleText("Date");
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    auto *axisY = new QValueAxis();
    axisY->setLabelFormat("%i");
    axisY->setTitleText("Count");
    axisY->setMin(0);
    axisY->setMax(maxCount + 1); 
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    ui->chartOrders->setChart(chart);
    ui->chartOrders->setRenderHint(QPainter::Antialiasing);
}
