#include "PaneInventory.h"
#include "ui_PaneInventory.h"

#include "inventory/InventoryInvoicesTree.h"
#include "inventory/InventoryTable.h"
#include "books/CompanyInfosTable.h"
#include "CurrencyRateManager.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "utils/CsvHeader.h"
#include "ExceptionWithTitleText.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QLocale>
#include <QSettings>

const QString PaneInventory::SETTINGS_KEY_AMZ_LEDGER_FOLDER = "inventory/amzLedgerDirectory";

PaneInventory::PaneInventory(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneInventory),
    m_invoicesTree(nullptr),
    m_inventoryTable(nullptr),
    m_companyInfos(nullptr),
    m_currRateManager(nullptr)
{
    ui->setupUi(this);
    ui->buttonExport->setEnabled(false);
    ui->spinBoxYear->setValue(QDate::currentDate().year()-1);

    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    
    // Load Amazon Ledger Folder from Settings
    QSettings settings;
    QString ledgerDir = settings.value(SETTINGS_KEY_AMZ_LEDGER_FOLDER).toString();
    if (ledgerDir.isEmpty() && workingDir.exists("amz_ledger")) {
        ledgerDir = workingDir.filePath("amz_ledger");
    }
    ui->lineEditAmzLedgerFolder->setText(ledgerDir);
    
    m_invoicesTree = new InventoryInvoicesTree(workingDir, this);
    ui->treeViewExtraPurchases->setModel(m_invoicesTree);
    ui->treeViewExtraPurchases->setHeaderHidden(true); // Maybe?
    
    _connectSlots();
}

PaneInventory::~PaneInventory()
{
    delete ui;
    // m_invoicesTree is child of this, deleted auto
    // m_companyInfos is child of this
    // m_currRateManager is child of this
    // m_inventoryTable needs check if it is child of tableView or this.
    // If setModel is used, view doesn't take ownership usually.
    if (m_inventoryTable) {
        delete m_inventoryTable;
    }
}

void PaneInventory::addExtraPurchase()
{
    QSettings settings;
    QString lastDir = settings.value("inventory/lastExtraPurchaseDir", QDir::homePath()).toString();
    
    QString filePath = QFileDialog::getOpenFileName(this, 
                                                    tr("Select Invoice"), 
                                                    lastDir, 
                                                    tr("CSV Files (*.csv *.CSV)"));
    
    if (filePath.isEmpty()) return;
    
    settings.setValue("inventory/lastExtraPurchaseDir", QFileInfo(filePath).absolutePath());
    
    m_invoicesTree->addFile(filePath);
}

void PaneInventory::removeExtraPurchase()
{
    QModelIndex index = ui->treeViewExtraPurchases->currentIndex();
    if (!index.isValid()) {
         QMessageBox::warning(this, tr("No Selection"), tr("Please select a file to remove."));
         return;
    }
    

    // Verify it is a file
    if (m_invoicesTree->data(index, Qt::UserRole).toString().isEmpty()) {
        QMessageBox::warning(this, tr("Remove"), tr("Please select a valid file item."));
        return;
    }
    
    m_invoicesTree->removeFile(index);
}

void PaneInventory::computeInventory()
{
    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    QDir purchasesDir = ui->widgetPurchaseExtraPurchases->getPurchaseDir(); // This is the MAIN purchase dir
    
    // Ledger Dir: Prefer "amz_ledger" if exists, else workingDir
    QDir amzLedgerDir = ui->lineEditAmzLedgerFolder->text();

    int year = ui->spinBoxYear->value();
    
    QHash<QString, double> prices;
    prices["US"] = ui->widgetPurchaseExtraPurchases->getShippingPrice("US");
    prices["CA"] = ui->widgetPurchaseExtraPurchases->getShippingPrice("CA");
    prices["UK"] = ui->widgetPurchaseExtraPurchases->getShippingPrice("UK");
    prices["JP"] = ui->widgetPurchaseExtraPurchases->getShippingPrice("JP");
    prices[""] = ui->widgetPurchaseExtraPurchases->getShippingPrice(""); // Default
    
    if (m_companyInfos) {
        delete m_companyInfos;
        m_companyInfos = nullptr;
    }
    m_companyInfos = new CompanyInfosTable(workingDir, this);
    
    if (m_currRateManager) {
        delete m_currRateManager;
        m_currRateManager = nullptr;
    }
    // Note: getApiKeyFixer might need the company info loaded first? 
    // CompanyInfosTable constructor loads data.
    m_currRateManager = new CurrencyRateManager(workingDir, m_companyInfos->getApiKeyFixer(), this);

    if (m_inventoryTable) {
        ui->tableViewInventory->setModel(nullptr);
        delete m_inventoryTable;
    }
    
    m_inventoryTable = new InventoryTable(workingDir, 
                                     purchasesDir, 
                                     amzLedgerDir, 
                                     year, 
                                     prices, 
                                     m_companyInfos, 
                                     m_currRateManager, 
                                     this); // Parent this
                                     
    setCursor(Qt::WaitCursor);
    try {
        m_inventoryTable->load();
        ui->tableViewInventory->setModel(m_inventoryTable);
        ui->tableViewInventory->resizeColumnsToContents();
        
        double totalValue = m_inventoryTable->getTotalValue();
        QString currency = m_companyInfos->getCurrency();
        
        QLocale locale(QLocale::English);
        QString formattedValue = locale.toString(totalValue, 'f', 2).replace(",", " ");
        
        ui->labelTotalValue->setText(QString("%1 %2").arg(formattedValue, currency));
        
        ui->buttonExport->setEnabled(true);
    } catch (const ExceptionWithTitleText &e) {
        setCursor(Qt::ArrowCursor);
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    } catch (const CsvHeaderException &e) {
        setCursor(Qt::ArrowCursor);
        QMessageBox::warning(this, tr("Header Error"), 
                             tr("In file %1, missing columns in CSV:\n%2").arg(
                                 e.getFileName(), e.columnValuesError().join("\n")));
    } catch (const std::exception &e) {
        setCursor(Qt::ArrowCursor);
        QMessageBox::warning(this, tr("Error"), tr("An unexpected error occurred: %1").arg(e.what()));
    }
    setCursor(Qt::ArrowCursor);
}

void PaneInventory::exportInventory()
{
    if (!m_inventoryTable) {
        return;
    }
    
    QSettings settings;
    QString lastDir = settings.value("inventory/lastExportDir", QDir::homePath()).toString();
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Inventory"), 
                                                    lastDir + "/inventory.csv", 
                                                    tr("CSV Files (*.csv)"));
                                                    
    if (fileName.isEmpty()) {
        return;
    }
    settings.setValue("inventory/lastExportDir", QFileInfo(fileName).absolutePath());
    
    m_inventoryTable->exportToCsv(fileName);
}

void PaneInventory::browseAmzLedgerFolderPath()
{
    QSettings settings;
    QString lastDir = settings.value(SETTINGS_KEY_AMZ_LEDGER_FOLDER, QDir::homePath()).toString();
    
    QString dir = QFileDialog::getExistingDirectory(this, 
                                                    tr("Select Amazon Ledger Folder"), 
                                                    lastDir, 
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    
    if (!dir.isEmpty()) {
        ui->lineEditAmzLedgerFolder->setText(dir);
        settings.setValue(SETTINGS_KEY_AMZ_LEDGER_FOLDER, dir);
    }
}

void PaneInventory::_connectSlots()
{
    connect(ui->buttonAddPurchase,
            &QPushButton::clicked,
            this,
            &PaneInventory::addExtraPurchase);
    connect(ui->buttonRemovePurchase,
            &QPushButton::clicked,
            this,
            &PaneInventory::removeExtraPurchase);
    connect(ui->buttonCompute,
            &QPushButton::clicked,
            this,
            &PaneInventory::computeInventory);
    connect(ui->buttonExport,
            &QPushButton::clicked,
            this,
            &PaneInventory::exportInventory);
    connect(ui->buttonBrowseLedgerFolder,
            &QPushButton::clicked,
            this,
            &PaneInventory::browseAmzLedgerFolderPath);
}
