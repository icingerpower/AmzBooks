#include "DialogPurchaseInvoices.h"
#include "ui_DialogPurchaseInvoices.h"
#include <QFileInfo>
#include <QMessageBox>
#include "ExceptionWithTitleText.h"

DialogPurchaseInvoices::DialogPurchaseInvoices(const BookAccountPurchaseTable *purchaseTable, const QStringList &filePaths, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogPurchaseInvoices),
    m_purchaseTable(purchaseTable),
    m_filePaths(filePaths)
{
    ui->setupUi(this);
    _setupTable();
    _populateTable();
}

DialogPurchaseInvoices::~DialogPurchaseInvoices()
{
    delete ui;
}

QList<PurchaseInformation> DialogPurchaseInvoices::selectedInvoices() const
{
    return m_validInvoices;
}

void DialogPurchaseInvoices::_setupTable()
{
    QStringList headers;
    headers << tr("File") << tr("Date") << tr("Supplier") << tr("Total") << tr("Status");
    ui->tableWidget->setColumnCount(headers.size());
    ui->tableWidget->setHorizontalHeaderLabels(headers);
}

void DialogPurchaseInvoices::_populateTable()
{
    ui->tableWidget->setRowCount(m_filePaths.size());
    m_validInvoices.clear();

    for (int i = 0; i < m_filePaths.size(); ++i) {
        QString filePath = m_filePaths[i];
        QFileInfo fi(filePath);
        QString fileName = fi.fileName();
        
        QTableWidgetItem *itemFile = new QTableWidgetItem(fileName);
        QTableWidgetItem *itemDate = new QTableWidgetItem();
        QTableWidgetItem *itemSupplier = new QTableWidgetItem();
        QTableWidgetItem *itemTotal = new QTableWidgetItem();
        QTableWidgetItem *itemStatus = new QTableWidgetItem();

        try {
            // We use decode to check validity
            // Note: decode uses the filename, not the full path, but here we pass the filepath to decode?
            // AbstractBooksTable::decode expects filename structure. 
            // PurchaseInvoiceManager::decode expects filename to decode info.
            PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, m_purchaseTable);
            // Also need to set filePath
            info.filePath = filePath;
            
            itemDate->setText(info.date.toString(Qt::ISODate));
            itemSupplier->setText(info.accountSupplier);
            itemTotal->setText(QString("%1 %2").arg(QString::number(info.totalAmount), info.currency));
            itemStatus->setText(tr("Valid"));
            
            // Set text color to green for valid
            itemStatus->setForeground(QBrush(Qt::darkGreen));
            
            m_validInvoices.append(info);

        } catch (const ExceptionWithTitleText &e) {
            itemStatus->setText(tr("Invalid: %1").arg(e.errorTitle()));
            itemStatus->setForeground(QBrush(Qt::red));
            itemStatus->setToolTip(e.errorText());
        } catch (...) {
             itemStatus->setText(tr("Error"));
             itemStatus->setForeground(QBrush(Qt::red));
        }

        ui->tableWidget->setItem(i, 0, itemFile);
        ui->tableWidget->setItem(i, 1, itemDate);
        ui->tableWidget->setItem(i, 2, itemSupplier);
        ui->tableWidget->setItem(i, 3, itemTotal);
        ui->tableWidget->setItem(i, 4, itemStatus);
    }
    
    ui->tableWidget->resizeColumnsToContents();
}
