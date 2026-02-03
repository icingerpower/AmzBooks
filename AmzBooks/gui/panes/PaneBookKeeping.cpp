#include <QDateTime>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "books/AbstractBooksTableBank.h"
#include "books/BooksConnections.h"
#include "banks/AbstractBankStatement.h"

#include <QTableView>
#include <QHeaderView>

#include "PaneBookKeeping.h"
#include "ui_PaneBookKeeping.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>

#include "../../common/utils/CsvHeader.h"
#include "books/ExceptionFileError.h"

PaneBookKeeping::PaneBookKeeping(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PaneBookKeeping)
{
    ui->setupUi(this);
    m_booksConnections = new BooksConnections{
            WorkingDirectoryManager::instance()->workingDir()};

    _initYears();
    _createBanks();
    _setSubButtonsEnabled(false);
    _connectSlots();
}

PaneBookKeeping::~PaneBookKeeping()
{
    delete ui;
    delete m_booksConnections;
}

void PaneBookKeeping::loadYearSelected()
{
    _setSubButtonsEnabled(true);
}

void PaneBookKeeping::generateBookKeeping()
{

}

void PaneBookKeeping::unselectAll()
{
    QList<QTableView *> allViews = this->findChildren<QTableView *>();
    for (auto &view : allViews)
    {
        view->clearSelection();
    }
}

void PaneBookKeeping::associate()
{

}

void PaneBookKeeping::dissociate()
{

}

void PaneBookKeeping::bankAdd()
{
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (!bankTable) {
        QMessageBox::warning(this, tr("No Bank Selected"), tr("Please select a bank first."));
        return;
    }

    const AbstractBankStatement *stmt = bankTable->getBankStatement();
    if (!stmt)
    {
        return;
    }

    QSettings settings;
    QString lastDir = settings.value("lastBankDir", QDir::homePath()).toString();
    QString filters = stmt->fileFilters().join(";;");

    QString fileName = QFileDialog::getOpenFileName(this, tr("Select Bank File"), lastDir, filters);
    
    if (fileName.isEmpty())
    {
        return;
    }

    QString dir = QFileInfo(fileName).absolutePath();
    settings.setValue("lastBankDir", dir);

    try {
        bankTable->addFilePaths({fileName});
    } catch (const CsvHeaderException &e) {
        QMessageBox::warning(this, tr("Import Error"), tr("CSV Header Error: Missing columns %1").arg(e.columnValuesError().join(", ")));
    } catch (const ExceptionFileError &e) {
        QMessageBox::warning(this, e.errorTitle(), e.errorText());
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Import Error"), tr("Error importing file: %1").arg(e.what()));
    } catch (...) {
        QMessageBox::warning(this, tr("Import Error"), tr("Unknown error importing file."));
    }
}

void PaneBookKeeping::bankRemove()
{
    AbstractBooksTableBank *bankTable = getVisibleBankTable();
    if (!bankTable)
    {
        return;
    }

    QTableView *view = qobject_cast<QTableView*>(ui->toolBoxBanks->currentWidget());
    if (!view)
    {
        return;
    }

    QModelIndexList selection = view->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select rows to remove."));
        return;
    }

    if (QMessageBox::question(this, tr("Confirm Removal"), 
                              tr("Are you sure you want to remove the selected rows? "
                                 "This will delete the associated files from disk.")) 
        != QMessageBox::Yes) {
        return;
    }

    try {
        bankTable->remove(selection);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Remove Error"), tr("Error removing files: %1").arg(e.what()));
    }
}

void PaneBookKeeping::_createBanks()
{
    // Remove existing pages
    while (ui->toolBoxBanks->count() > 0) {
        QWidget *widget = ui->toolBoxBanks->widget(0);
        ui->toolBoxBanks->removeItem(0);
        delete widget;
    }

    // Browse AbstractBooksTableBank::ALL_TABLES()
    const auto &tables = AbstractBooksTableBank::ALL_TABLES();
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        
        // Create Table View
        QTableView *view = new QTableView(this);
        // Create table model
        AbstractBooksTableBank *bankTable = it.value()(m_booksConnections,
                                                       WorkingDirectoryManager::instance()->workingDir(),
                                                       view);
        view->setModel(bankTable);
        view->setAlternatingRowColors(true);
        view->horizontalHeader()->setStretchLastSection(true);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);

        // Add to toolBox
        QString name = bankTable->getBankStatement() ? bankTable->getBankStatement()->getName() : "Unknown";
        ui->toolBoxBanks->addItem(view, name);
    }
}

void PaneBookKeeping::_initYears()
{
    int currentYear = QDate::currentDate().year();
    for (int i=0; i<5; ++i) {
        ui->comboBoxYear->addItem(QString::number(currentYear - i));
    }
}

void PaneBookKeeping::_connectSlots()
{
    connect(ui->buttonLoadYear,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::loadYearSelected);
    connect(ui->buttonGenBooks,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::generateBookKeeping);
    connect(ui->buttonUnselectAll,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::unselectAll);
    connect(ui->buttonBankAddStatement,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::bankAdd);
    connect(ui->buttonBankRemove,
            &QPushButton::clicked,
            this,
            &PaneBookKeeping::bankRemove);
}

void PaneBookKeeping::_setSubButtonsEnabled(bool enabled)
{
    // Browse every QPushButton of the main class
    QList<QPushButton *> allButtons = this->findChildren<QPushButton *>();
    
    for (QPushButton *btn : allButtons) {
        if (btn != ui->buttonLoadYear)
        {
            // It is NOT in the main list, so we enable/disable it
            btn->setEnabled(enabled);
        }
    }
}

AbstractBooksTableBank *PaneBookKeeping::getVisibleBankTable() const
{
    QWidget *currentWidget = ui->toolBoxBanks->currentWidget();
    if (auto *view = qobject_cast<QTableView*>(currentWidget)) {
        return qobject_cast<AbstractBooksTableBank*>(view->model());
    }
    return nullptr;
}
