#include "books/SkuRegradedTable.h"

#include "DialogMapSkuRegraded.h"
#include "ui_DialogMapSkuRegraded.h"

DialogMapSkuRegraded::DialogMapSkuRegraded(SkuRegradedTable *skuRegradedTable,
                                           QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogMapSkuRegraded)
{
    ui->setupUi(this);
    ui->tableView->setModel(skuRegradedTable);
    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setStretchLastSection(true);
}

DialogMapSkuRegraded::~DialogMapSkuRegraded()
{
    delete ui;
}
