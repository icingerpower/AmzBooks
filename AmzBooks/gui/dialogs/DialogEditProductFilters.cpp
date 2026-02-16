#include "DialogEditProductFilters.h"
#include "ui_DialogEditProductFilters.h"
#include "profit/ProductFilterTable.h"
#include <QInputDialog>
#include <QMessageBox>

DialogEditProductFilters::DialogEditProductFilters(ProductFilterTable *table, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditProductFilters),
    m_table(table)
{
    ui->setupUi(this);
    ui->tableView->setModel(m_table);
    
    connect(ui->buttonAdd, &QPushButton::clicked, this, &DialogEditProductFilters::onAdd);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &DialogEditProductFilters::onRemove);
}

DialogEditProductFilters::~DialogEditProductFilters()
{
    delete ui;
}

void DialogEditProductFilters::onAdd()
{
    if (!m_table) return;
    
    // Simple add with default values or ask name?
    // Requirement says "add button to add / remove".
    // I'll add an empty row or ask for name.
    // ProductFilterTable::addFilter takes (name, filters).
    
    // Let's ask for the filter name
    bool ok;
    QString name = QInputDialog::getText(this, tr("Add Filter"),
                                         tr("Filter Name:"), QLineEdit::Normal,
                                         "", &ok);
    if (ok && !name.isEmpty()) {
        m_table->addFilter(name, "");
    }
}

void DialogEditProductFilters::onRemove()
{
    if (!m_table) return;
    
    QModelIndexList selected = ui->tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;
    
    // Remove rows reverse order to maintain indices
    // Sort selected rows
    QList<int> rows;
    for (const QModelIndex &idx : selected) rows.append(idx.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    
    for (int row : rows) {
        m_table->removeRows(row, 1);
    }
}
