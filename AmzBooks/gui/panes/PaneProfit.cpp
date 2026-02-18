#include "../../../../common/workingdirectory/WorkingDirectoryManager.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QTimer>

#include "profit/ProductFilterTable.h"
#include "gui/dialogs/DialogEditProductFilters.h"
#include "profit/ProfitTree.h"
#include "books/CompanyInfosTable.h"
#include "utils/CsvHeader.h"
#include "CountriesEu.h"
#include "CurrencyRateManager.h"
#include "ExceptionWithTitleText.h"

#include "PaneProfit.h"
#include "ui_PaneProfit.h"

const QString PaneProfit::SETTINGS_KEY_ECONOMIC_FOLDER = "profit/economicFolder";

PaneProfit::PaneProfit(QWidget *parent) :
    QWidget(parent),

    ui(new Ui::PaneProfit),
    m_productFilterTable(nullptr),
    m_profitTree(nullptr),
    m_companyInfos(nullptr),
    m_currRateManager(nullptr)
{
    ui->setupUi(this);
    
    auto settings = WorkingDirectoryManager::instance()->settings();
    QString economicFolder = settings->value(SETTINGS_KEY_ECONOMIC_FOLDER).toString();
    
    // Load start date days diff
    int daysDiff = settings->value("profit/startDateDaysDiff", 90).toInt();
    QDate startDate = QDate::currentDate().addDays(-daysDiff);
    startDate.setDate(startDate.year(), startDate.month(), 1); // Day 1
    ui->dateEditSartDate->setDate(startDate);
    
    // Load min units
    int minUnits = settings->value("profit/minUnits", 0).toInt();
    ui->spinBoxMinUnits->setValue(minUnits);

    if (!economicFolder.isEmpty() && QDir(economicFolder).exists()) {
        ui->lineEditEconomicsFolder->setText(economicFolder);
    }
    _setFilterButtonsEnabled(false);
    
    // Create ProductFilterTable with WorkingDirectoryManager::workingDir
    m_productFilterTable = new ProductFilterTable{
            WorkingDirectoryManager::instance()->workingDir(), this};
            
    ui->comboBoxFilter->setModel(m_productFilterTable);
    ui->comboBoxFilter->setModelColumn(0);
    
    _connectSlots();
}

PaneProfit::~PaneProfit()
{
    delete ui;
}

void PaneProfit::saveSettings()
{
    auto settings = WorkingDirectoryManager::instance()->settings();
    // Save start date days diff (days TO today)
    // If startDate is in future, daysDiff is negative? 
    // Logic: today - date = diff. So date = today - diff.
    // daysTo: date.daysTo(today).
    int daysDiff = ui->dateEditSartDate->date().daysTo(QDate::currentDate());
    settings->setValue("profit/startDateDaysDiff", daysDiff);
    
    settings->setValue("profit/minUnits", ui->spinBoxMinUnits->value());
}

void PaneProfit::browseEconomicFolder()
{
    QString currentDir = ui->lineEditEconomicsFolder->text();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Economic Folder"),
                                                    currentDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    
    if (!dir.isEmpty()) {
        ui->lineEditEconomicsFolder->setText(dir);
        
        auto settings = WorkingDirectoryManager::instance()->settings();
        settings->setValue(SETTINGS_KEY_ECONOMIC_FOLDER, dir);
    }
}

void PaneProfit::computeProfit()
{
    QString dirPath = ui->lineEditEconomicsFolder->text();
    if (dirPath.isEmpty() || !QDir(dirPath).exists()) {
        QMessageBox::warning(
                    this,
                    tr("No economics folder"),
                    tr("The economics folder is needed and should exist"));
        return;
    }
    QDir economicsDir(dirPath);
    
    if (m_companyInfos != nullptr) {
        m_companyInfos->deleteLater();
    }
    m_companyInfos = new CompanyInfosTable(WorkingDirectoryManager::instance()->workingDir(), this);
    
    if (!m_currRateManager) {
        m_currRateManager = new CurrencyRateManager(WorkingDirectoryManager::instance()->workingDir(), 
                                                    m_companyInfos->getApiKeyFixer(), this);
    }
    
    if (m_profitTree) {
        delete m_profitTree;
        m_profitTree = nullptr;
    }
    
    auto currentProfitTree = ui->treeViewProfit->model();
    if (currentProfitTree != nullptr)
    {
        ui->treeViewProfit->setModel(nullptr);
        currentProfitTree->deleteLater();
    }
    double avgPricePerKilo = ui->widgetPurchases->getShippingPrice("");

    m_profitTree = new ProfitTree(WorkingDirectoryManager::instance()->workingDir(), // Settings
                                  economicsDir, // Economics
                                  ui->widgetPurchases->getPurchaseDir(),
                                  ui->dateEditSartDate->date(), 
                                  ui->spinBoxMinUnits->value(), 
                                  avgPricePerKilo,
                                  m_companyInfos, 
                                  m_currRateManager, 
                                  ui->treeViewProfit);
    setCursor(Qt::WaitCursor);
    
    try {
        m_profitTree->load();
        ui->treeViewProfit->setModel(m_profitTree);
        ui->treeViewProfit->resizeColumnToContents(0);
        ui->treeViewProfit->header()->resizeSection(
                    ProfitTree::COL_MSKU, 200);
        ui->treeViewProfit->header()->resizeSection(
                    ProfitTree::COL_TITLE, 400);
        ui->treeViewProfit->header()->resizeSection(
                    ProfitTree::COL_UNITS_RETURNED, 80);
        ui->treeViewProfit->header()->resizeSection(
                    ProfitTree::COL_RETURN_PERCENT, 80);
        
        // Total Columns
        ui->treeViewProfit->header()->resizeSection(ProfitTree::COL_TOTAL_ADS, 80);
        ui->treeViewProfit->header()->resizeSection(ProfitTree::COL_TOTAL_STORAGE, 80);
        ui->treeViewProfit->header()->resizeSection(ProfitTree::COL_TOTAL_FBA_FEES, 80);
        ui->treeViewProfit->header()->resizeSection(ProfitTree::COL_TOTAL_REFERRAL_FEES, 80);
        ui->treeViewProfit->header()->resizeSection(ProfitTree::COL_TOTAL_OTHER_FEES, 80);
        ui->treeViewProfit->header()->resizeSection(ProfitTree::COL_TOTAL_AMZ_COSTS, 100);
        _setFilterButtonsEnabled(true);
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

void PaneProfit::editFilters()
{
    if (!m_productFilterTable) {
        return;
    }
    DialogEditProductFilters dialog(m_productFilterTable, this);
    dialog.exec();
}

void PaneProfit::filter()
{
    if (!m_profitTree) {
        return;
    }
    
    int row = ui->comboBoxFilter->currentIndex();
    QStringList filters = m_productFilterTable->getFilters(row);
    if (filters.isEmpty()) {
        filterReset();
        return;
    }
    
    // Iterate root children
    // m_profitTree is a tree. Parent items are children of root.
    // We check Parent items.
    
    for (int i = 0; i < m_profitTree->rowCount(); ++i) {
        bool match = false;
        
        QString t0 = m_profitTree->data(m_profitTree->index(i, ProfitTree::COL_PARENT_ASIN)).toString();
        QString t1 = m_profitTree->data(m_profitTree->index(i, ProfitTree::COL_MSKU)).toString();
        QString t2 = m_profitTree->data(m_profitTree->index(i, ProfitTree::COL_TITLE)).toString();
        QString t3 = m_profitTree->data(m_profitTree->index(i, ProfitTree::COL_ASIN)).toString();
        
        for (const QString &f : filters) {
            if (t0.contains(f, Qt::CaseInsensitive) || 
                t1.contains(f, Qt::CaseInsensitive) || 
                t2.contains(f, Qt::CaseInsensitive) ||
                t3.contains(f, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        
        ui->treeViewProfit->setRowHidden(i, QModelIndex(), !match);
    }
}

void PaneProfit::filterReset()
{
    if (!m_profitTree) return;
    for (int i = 0; i < m_profitTree->rowCount(); ++i) {
        ui->treeViewProfit->setRowHidden(i, QModelIndex(), false);
    }
}

void PaneProfit::_connectSlots()
{
    connect(ui->buttonBrowseEconomicsFolder,
            &QPushButton::clicked,
            this,
            &PaneProfit::browseEconomicFolder);
    connect(ui->buttonFilter,
            &QPushButton::clicked,
            this,
            &PaneProfit::filter);
    connect(ui->buttonFilterReset,
            &QPushButton::clicked,
            this,
            &PaneProfit::filterReset);
    connect(ui->buttonEditFilters,
            &QPushButton::clicked,
            this,
            &PaneProfit::editFilters);
    connect(ui->pushButton,
            &QPushButton::clicked,
            this,
            &PaneProfit::computeProfit);
            
    connect(ui->dateEditSartDate, &QDateEdit::dateChanged, this, &PaneProfit::saveSettings);
    connect(ui->spinBoxMinUnits, QOverload<int>::of(&QSpinBox::valueChanged), this, &PaneProfit::saveSettings);
}

void PaneProfit::_setFilterButtonsEnabled(bool enable)
{
    ui->buttonEditFilters->setEnabled(enable);
    ui->buttonFilter->setEnabled(enable);
    ui->buttonFilterReset->setEnabled(enable);
}
