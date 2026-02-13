#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QCoroTask>
#include <QException>

#include "../../../../common/workingdirectory/WorkingDirectoryManager.h"

#include "orders/FileImportersTable.h"
#include "orders/ParamsTable.h"
#include "orders/AbstractImporterFile.h"
#include "orders/OrderManager.h"

#include "PaneOrderFiles.h"
#include "ui_PaneOrderFiles.h"
#include "gui/dialogs/DialogVatParams.h"
#include "gui/dialogs/DialogVatParams.h"
#include "gui/dialogs/DialogViewOrders.h"
#include "utils/CsvHeader.h"
#include "books/ActivityTable.h"
#include "books/CompanyInfosTable.h"
#include "CurrencyRateManager.h"

#include <QFileSystemModel>

PaneOrderFiles::PaneOrderFiles(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneOrderFiles),
    m_importersTable(new FileImportersTable(this)),
    m_fileSystemModel(new QFileSystemModel(this))
{
    ui->setupUi(this);

    // Setup Importers Table
    ui->tableImporters->setModel(m_importersTable);
    ui->tableImporters->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableImporters->horizontalHeader()->setStretchLastSection(true);
    ui->tableImporters->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableImporters->setSelectionMode(QAbstractItemView::SingleSelection);

    // Setup Params Table (Headers will be set when model is set)
    ui->tableParams->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // Setup Params Table (Headers will be set when model is set)
    ui->tableParams->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableParams->horizontalHeader()->setStretchLastSection(true);

    // Setup Orders Table
    ui->tableOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableOrders->horizontalHeader()->setStretchLastSection(true);
    ui->tableOrders->setSortingEnabled(true);

    _connectSlots();

    ui->tableImporters->setCurrentIndex(ui->tableImporters->model()->index(0, 0));
}

PaneOrderFiles::~PaneOrderFiles()
{
    delete ui;
}

void PaneOrderFiles::_connectSlots()
{
    connect(ui->buttonImport, &QPushButton::clicked, this, &PaneOrderFiles::importFile);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &PaneOrderFiles::removeFile);

    connect(ui->tableImporters->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            &PaneOrderFiles::onImporterSelected);
}

void PaneOrderFiles::onImporterSelected(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid()) {
        ui->tableParams->setModel(nullptr);
        ui->treeViewFiles->setModel(nullptr);
        if (m_paramsModel) {
            delete m_paramsModel;
            m_paramsModel = nullptr;
        }
        return;
    }

    AbstractImporterFile *importer = m_importersTable->getImporter(current);
    if (!importer) {
        ui->tableParams->setModel(nullptr);
        ui->treeViewFiles->setModel(nullptr);
        return;
    }

    // Ensure params are loaded/initialized if not already
    // importer->load(); // Assuming load() is cheap or idempotent. 
    // Actually FileImportersTable might not load them by default.
    // AbstractImporter::load() accesses settings.
    
    // Create new ParamsTable
    // Note: We create a new model each time selection changes.
    auto *oldModel = m_paramsModel;
    m_paramsModel = new ParamsTable(importer, this);
    connect(m_paramsModel, &ParamsTable::exceptionOccurred, this, [this](const QString &title, const QString &message) {
        QMessageBox::warning(this, title, message);
    });
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

    // Init treeViewFiles
    QString workingDir = AbstractImporterFile::GET_WORKING_DIR(
        WorkingDirectoryManager::instance()->workingDir(), 
        importer->getId()
    );
    
    // Create directory if it doesn't exist? Optional but good practice if expected.
    QDir dir(workingDir);
    if (!dir.exists()) dir.mkpath(".");

    m_fileSystemModel->setRootPath(workingDir);
    ui->treeViewFiles->setModel(m_fileSystemModel);
    ui->treeViewFiles->setRootIndex(m_fileSystemModel->index(workingDir));
    
    // Init Date Edits
    updateChart();
    
    auto dates = importer->datesFromTo();
    if (dates.first.isValid()) {
        ui->dateEditMin->setDate(dates.first.date());
    } else {
        ui->dateEditMin->setDate(QDate::currentDate()); // Default?
    }
    
    if (dates.second.isValid()) {
        ui->dateEditMax->setDate(dates.second.date());
    } else {
        ui->dateEditMax->setDate(QDate::currentDate());
    }
}

void PaneOrderFiles::importFile()
{
    QModelIndex index = ui->tableImporters->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Import"), tr("Please select an importer first."));
        return;
    }

    AbstractImporterFile *importer = m_importersTable->getImporter(index);
    if (!importer) return;

    QString filePath = QFileDialog::getOpenFileName(this, tr("Select File to Import"),
                                                    QDir::homePath(),
                                                    tr("All Files (*.*)")); // Filters could be improved based on importer
    if (filePath.isEmpty()) return;

    // Run import asynchronously
    // We utilize QCoro to handle the async task
    
    [](PaneOrderFiles *self, AbstractImporterFile *importer, QString path) -> QCoro::Task<void> {
        // Disable UI?
        self->ui->buttonImport->setEnabled(false);
        
        try {
            // Create callback for missing data - opens DialogVatParams
            auto callbackAddIfMissing = [self](const QString &errorTitle, const QString &errorText) -> QCoro::Task<bool> {
                DialogVatParams dialog(errorTitle, errorText, self);
                int result = dialog.exec();
                co_return (result == QDialog::Accepted);
            };
            
            // Load Report with callback
            auto result = co_await importer->loadReport(path, callbackAddIfMissing);
            
            if (!result.errorReturned.isEmpty()) {
                QMessageBox::critical(self, tr("Import Failed"), result.errorReturned);
            } else {
                // Save to OrderManager
                // We instantiate OrderManager here. 
                // CAUTION: Ensure DB connection doesn't conflict if app uses shared connection.
                // Assuming OrderManager handles its own connection or default connection is safe.

                int importedCount = 0;
                if (result.orderInfos) {
                   // Preview Dialog
                   QDir workingDir(WorkingDirectoryManager::instance()->workingDir());
                   CompanyInfosTable companyInfo(workingDir);
                   CurrencyRateManager currencyRateManager(workingDir, companyInfo.getApiKeyFixer());
                   
                   DialogViewOrders dialog(*result.orderInfos, &currencyRateManager, companyInfo.getCurrency(), self);
                   if (dialog.exec() != QDialog::Accepted) {
                       self->ui->buttonImport->setEnabled(true);
                       co_return;
                   }

                   OrderManager manager(workingDir);

                   // We need to store the value in a local variable because getActivitySource() returns by value (temporary),
                   // and we cannot take the address of a temporary.
                   ActivitySource source = importer->getActivitySource();

                   // Process Shipments
                   for (const auto &shipment : result.orderInfos->shipments) {
                       manager.recordShipmentFromSource(
                           shipment.getId(),
                           &source,
                           &shipment,
                           QDate() // No conflict date override
                       );
                   }
                   
                   // Process Refunds
                   for (const auto &refund : result.orderInfos->refunds) {
                       manager.recordShipmentFromSource(
                           refund.getId(),
                           &source,
                           &refund,
                           QDate()
                       );
                   }
                   
                   // Process Addresses
                   for (const auto &addr : result.orderInfos->orderAddresses) {
                       manager.recordAddressTo(addr.orderId, addr.address);
                   }
                   
                   // Process InvoicingInfos
                   for (const auto &inv : result.orderInfos->invoicingInfos) {
                       manager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
                   }
                   
                   importedCount = result.orderInfos->shipments.size() + result.orderInfos->refunds.size();

                   // Update Chart Data
                   if (importedCount > 0) {
                       auto &counts = self->m_ordersData[importer->getId()];
                       for (const auto &shipment : result.orderInfos->shipments) {
                           // Using first activity date as approximation for shipment date
                           if (!shipment.getActivities().isEmpty()) {
                               counts[shipment.getActivities().first().getDateTime().date()]++;
                           }
                       }
                       for (const auto &refund : result.orderInfos->refunds) {
                           if (!refund.getActivities().isEmpty()) {
                               counts[refund.getActivities().first().getDateTime().date()]++;
                           }
                       }
                       
                       // Trigger chart update on main thread
                       QMetaObject::invokeMethod(self, "updateChart", Qt::QueuedConnection);
                       
                       // Add activities to table on main thread (or safe way)
                       // Since we are in lambda, we can access self.
                       // Thread correctness: The lambda runs in QCoro continuation which might be main thread if started from there?
                       // QCoro::Task usually resumes on the context it started. importFile called from UI thread.
                       // However, let's be safe or just do it.
                       
                       QList<Activity> newActivities;
                       for (const auto &shipment : result.orderInfos->shipments) {
                           newActivities.append(shipment.getActivities());
                       }
                       for (const auto &refund : result.orderInfos->refunds) {
                           newActivities.append(refund.getActivities());
                       }
                       
                       if (!newActivities.isEmpty()) {
                           // Using invokeMethod to ensure we modify model on main thread if not already
                            QMetaObject::invokeMethod(self, [self, importerId = importer->getId(), newActivities]() {
                                if (self->m_activityModels.contains(importerId)) {
                                    self->m_activityModels[importerId]->addActivities(newActivities);
                                } else {
                                     // Should have been created in onImporterSelected, but if not:
                                     auto *table = new ActivityTable(self);
                                     table->addActivities(newActivities);
                                     self->m_activityModels[importerId] = table;
                                     // If this importer is currently selected, update view
                                     // We checked onImporterSelected for validity, but let's see.
                                     // Simple check:
                                     QModelIndex index = self->ui->tableImporters->currentIndex();
                                     if (index.isValid()) {
                                         auto *currentImporter = self->m_importersTable->getImporter(index);
                                         if (currentImporter && currentImporter->getId() == importerId) {
                                             self->ui->tableOrders->setModel(table);
                                         }
                                     }
                                }
                            }, Qt::QueuedConnection);
                       }
                   }
                }
                
                QMessageBox::information(self, tr("Import Successful"), 
                                         tr("Successfully imported %1 items.").arg(importedCount));
            }
            
        } catch (const CsvHeaderException &e) {
             QMessageBox::critical(self, tr("Import Error"), e.what());
        } catch (const QException &e) {
             QMessageBox::critical(self, tr("Import Error"), e.what());
        } catch (const std::exception &e) {
             QMessageBox::critical(self, tr("Import Error"), QString(e.what()));
        }

        self->ui->buttonImport->setEnabled(true);
        
    }(this, importer, filePath);
}

void PaneOrderFiles::removeFile()
{
    QModelIndex index = ui->treeViewFiles->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, tr("Remove File"), tr("Please select a file to remove."));
        return;
    }

    // Check if it's a file, not a directory (unless we want to support recursive delete?)
    // Requirements say "remove selected file" usually.
    if (m_fileSystemModel->isDir(index)) {
        QMessageBox::warning(this, tr("Remove File"), tr("Please select a file, not a directory."));
        return;
    }
    
    QString fileName = m_fileSystemModel->fileName(index);
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Remove File"), 
                                  tr("Are you sure you want to delete '%1'?").arg(fileName),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        if (!m_fileSystemModel->remove(index)) {
            QMessageBox::critical(this, tr("Remove Error"), tr("Could not delete file: %1").arg(fileName));
        }
    }
}

void PaneOrderFiles::updateChart()
{
    // Clear previous chart
    ui->chartOrders->setChart(new QChart());
    
    QModelIndex index = ui->tableImporters->currentIndex();
    if (!index.isValid()) return;
    
    AbstractImporterFile *importer = m_importersTable->getImporter(index);
    if (!importer) return;
    
    const auto &counts = m_ordersData[importer->getId()];
    if (counts.isEmpty()) return;
    
    auto *series = new QLineSeries();
    series->setName(tr("Orders"));
    
    int maxCount = 0;
    
    // Sort keys just in case (QMap is sorted by key)
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        // Convert date to milliseconds for QDateTimeAxis
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
    axisY->setMax(maxCount + 1); // Add some padding
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    ui->chartOrders->setChart(chart);
    ui->chartOrders->setRenderHint(QPainter::Antialiasing);
}
