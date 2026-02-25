#include "DialogAmzPayments.h"
#include "ui_DialogAmzPayments.h"

#include <QBrush>
#include <QFileInfo>
#include <QTableWidgetItem>

#include "ExceptionWithTitleText.h"

DialogAmzPayments::DialogAmzPayments(const QStringList &filePaths, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogAmzPayments)
    , m_filePaths(filePaths)
{
    ui->setupUi(this);
    _setupTable();
    _populateTable();
}

DialogAmzPayments::~DialogAmzPayments()
{
    delete ui;
}

QList<AmzPaymentInfo> DialogAmzPayments::selectedPayments() const
{
    return m_validPayments;
}

void DialogAmzPayments::_setupTable()
{
    QStringList headers;
    headers << tr("File") << tr("Country") << tr("Date From")
            << tr("Date To") << tr("Paid") << tr("Status");
    ui->tableWidget->setColumnCount(headers.size());
    ui->tableWidget->setHorizontalHeaderLabels(headers);
}

void DialogAmzPayments::_populateTable()
{
    ui->tableWidget->setRowCount(m_filePaths.size());
    m_validPayments.clear();

    for (int i = 0; i < m_filePaths.size(); ++i) {
        const QString &filePath = m_filePaths[i];
        QFileInfo fi(filePath);

        QTableWidgetItem *itemFile     = new QTableWidgetItem(fi.fileName());
        QTableWidgetItem *itemCountry  = new QTableWidgetItem();
        QTableWidgetItem *itemDateFrom = new QTableWidgetItem();
        QTableWidgetItem *itemDateTo   = new QTableWidgetItem();
        QTableWidgetItem *itemPaid     = new QTableWidgetItem();
        QTableWidgetItem *itemStatus   = new QTableWidgetItem();

        try {
            AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(filePath);

            itemCountry->setText(info.countryCode);
            itemDateFrom->setText(info.dateFrom.toString(Qt::ISODate));
            itemDateTo->setText(info.dateTo.toString(Qt::ISODate));
            itemPaid->setText(QString("%1 %2")
                                  .arg(info.paid, 0, 'f', 2)
                                  .arg(info.paidCurrency));
            if (!info.hasBalanceStart && !info.hasBalanceEnd) {
                itemStatus->setText(tr("Valid (warning: no balance info)"));
                itemStatus->setForeground(QBrush(QColor(200, 100, 0))); // dark orange
                itemStatus->setToolTip(tr("balance-begin and balance-end are both absent – balances will be treated as 0.00"));
            } else {
                itemStatus->setText(tr("Valid"));
                itemStatus->setForeground(QBrush(Qt::darkGreen));
            }

            m_validPayments.append(info);

        } catch (const ExceptionWithTitleText &e) {
            itemStatus->setText(tr("Invalid: %1").arg(e.errorTitle()));
            itemStatus->setForeground(QBrush(Qt::red));
            itemStatus->setToolTip(e.errorText());
        } catch (...) {
            itemStatus->setText(tr("Error"));
            itemStatus->setForeground(QBrush(Qt::red));
        }

        ui->tableWidget->setItem(i, 0, itemFile);
        ui->tableWidget->setItem(i, 1, itemCountry);
        ui->tableWidget->setItem(i, 2, itemDateFrom);
        ui->tableWidget->setItem(i, 3, itemDateTo);
        ui->tableWidget->setItem(i, 4, itemPaid);
        ui->tableWidget->setItem(i, 5, itemStatus);
    }

    ui->tableWidget->resizeColumnsToContents();
}
