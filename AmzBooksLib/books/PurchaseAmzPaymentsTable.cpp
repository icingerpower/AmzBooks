#include "PurchaseAmzPaymentsTable.h"
#include "CompanyInfosTable.h"
#include "CurrencyRateManager.h"
#include <QFileInfo>

PurchaseAmzPaymentsTable::PurchaseAmzPaymentsTable(
        const BooksConnections *bookConnections,
        const QDir &workingDir,
        QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
    , m_workingDir(workingDir)
{
    m_manager  = new PurchaseAmzPaymentsManager(workingDir, this);
    m_settings = new AmzPaymentSettings(workingDir, this);

    CompanyInfosTable companyInfos{workingDir};
    m_companyCurrency = companyInfos.getCurrency();
    m_currencyRateManager = new CurrencyRateManager(workingDir, companyInfos.getApiKeyFixer(), this);
}

QString PurchaseAmzPaymentsTable::getId() const
{
    return "amazon-payments";
}

static void storeConverted(QHash<QString, double> &convertedAmounts,
                           const QString &rowId,
                           const AmzPaymentInfo &info,
                           const QString &companyCurrency,
                           CurrencyRateManager *crm)
{
    if (!info.dateTo.isValid())
        return;
    try {
        convertedAmounts[rowId] = crm->convert(info.paid, info.paidCurrency, companyCurrency, info.dateTo);
    } catch (...) {
        // rate not available
    }
}

void PurchaseAmzPaymentsTable::load(int year)
{
    QDate start(year, 1, 1);
    QDate end(year, 12, 31);

    QList<AmzPaymentInfo> payments = m_manager->getPayments(start, end);

    for (const AmzPaymentInfo &info : payments) {
        // Use the file's base name as the row ID (same pattern as PurchaseInvoiceTable)
        QString rowId = QFileInfo(info.filePath).fileName();

        AbstractBooksTable::add(rowId, "",
            info.dateTo,
            info.paid,
            info.paidCurrency,
            QString("Amazon payment – %1").arg(info.countryCode),
            m_settings->getAccountDebit(),
            m_settings->getAccountCredit(),
            0.0,  // no VAT on disbursements
            "",
            info.paidCurrency);

        storeConverted(m_convertedAmounts, rowId, info, m_companyCurrency, m_currencyRateManager);
    }
}

QList<AmzPaymentInfo> PurchaseAmzPaymentsTable::getPayments(const QDate &from, const QDate &to) const
{
    return m_manager->getPayments(from, to);
}

PurchaseAmzPaymentsManager &PurchaseAmzPaymentsTable::manager() const
{
    return *m_manager;
}

void PurchaseAmzPaymentsTable::add(const QString &sourceFilePath, const AmzPaymentInfo &info)
{
    m_manager->add(sourceFilePath, info);

    // Build the row ID that was just created (mirrors PurchaseAmzPaymentsManager::add logic)
    QString ext = QFileInfo(sourceFilePath).suffix().toLower();
    QString rowId = PurchaseAmzPaymentsManager::encode(info)
                    + (ext.isEmpty() ? QString() : "." + ext);

    // Insert a single row using beginInsertRows / endInsertRows (via AbstractBooksTable::add)
    AbstractBooksTable::add(rowId, "",
        info.dateTo,
        info.paid,
        info.paidCurrency,
        QString("Amazon payment \u2013 %1").arg(info.countryCode),
        m_settings->getAccountDebit(),
        m_settings->getAccountCredit(),
        0.0,
        "",
        info.paidCurrency);

    storeConverted(m_convertedAmounts, rowId, info, m_companyCurrency, m_currencyRateManager);
}

void PurchaseAmzPaymentsTable::removePayment(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    removePayment(getRowId(index));
}

void PurchaseAmzPaymentsTable::removePayment(const QString &rowId)
{
    if (m_manager->remove(rowId))
        remove(rowId);
}

// AbstractBooksTable has 9 standard columns; add 6 Amazon-specific extras:
//   convertedAmount, dateFrom, balanceStart, balanceEnd, expenses, refundedExpenses
static const int EXTRA_COLS = 6;

int PurchaseAmzPaymentsTable::columnCount(const QModelIndex &parent) const
{
    return AbstractBooksTable::columnCount(parent) + EXTRA_COLS;
}

QVariant PurchaseAmzPaymentsTable::headerData(int section,
                                               Qt::Orientation orientation,
                                               int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        int base = AbstractBooksTable::columnCount();
        switch (section - base) {
        case 0: return tr("Converted Amount");
        case 1: return tr("Date From");
        case 2: return tr("Balance Start");
        case 3: return tr("Balance End");
        case 4: return tr("Expenses");
        case 5: return tr("Refunded");
        }
    }
    return AbstractBooksTable::headerData(section, orientation, role);
}

QVariant PurchaseAmzPaymentsTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    int base = AbstractBooksTable::columnCount();
    if (index.column() < base) {
        return AbstractBooksTable::data(index, role);
    }

    if (role == Qt::BackgroundRole) {
        return AbstractBooksTable::data(this->index(index.row(), 0), role);
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }

    // Decode the AmzPaymentInfo from the row ID (filename), same approach as PurchaseInvoiceTable
    QString rowId = getRowId(index);
    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(rowId);

    switch (index.column() - base) {
    case 0: {
        if (!m_convertedAmounts.contains(rowId))
            return QString();
        double c = m_convertedAmounts[rowId];
        return QString("%1 %2").arg(c, 0, 'f', 2).arg(m_companyCurrency);
    }
    case 1: return info.dateFrom;
    case 2: return QString("%1 %2")
                        .arg(info.balanceStart, 0, 'f', 2)
                        .arg(info.balanceStartCurrency);
    case 3: return QString("%1 %2")
                        .arg(info.balanceEnd, 0, 'f', 2)
                        .arg(info.balanceEndCurrency);
    case 4: return info.hasExpenses
                ? QString("%1 %2").arg(info.expenses, 0, 'f', 2)
                                   .arg(info.expensesCurrency)
                : QString();
    case 5: return info.hasRefundedExpenses
                ? QString("%1 %2").arg(info.refundedExpenses, 0, 'f', 2)
                                   .arg(info.refundedExpensesCurrency)
                : QString();
    }
    return QVariant();
}
