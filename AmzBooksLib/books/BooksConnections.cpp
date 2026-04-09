#include <QFile>
#include <QTextStream>
#include <cmath>
#include <QDebug>
#include "AbstractBooksTable.h"
#include "AbstractBooksTableBank.h"
#include "EntrySelfTable.h"
#include "ExceptionWithTitleText.h"

#include "CurrencyRateManager.h"

#include "BooksConnections.h"
#include "CompanyInfosTable.h"

// Fixed approximate EUR conversion rates (mirrors PurchaseAmzPaymentsManager::toEur)
static double toEurApprox(double amount, const QString &currency)
{
    if (currency == "EUR") return amount;
    if (currency == "USD") return amount * 0.92;
    if (currency == "GBP") return amount * 1.16;
    if (currency == "CAD") return amount * 0.68;
    if (currency == "JPY") return amount * 0.0062;
    if (currency == "AUD") return amount * 0.60;
    if (currency == "MXN") return amount * 0.046;
    if (currency == "SEK") return amount * 0.087;
    if (currency == "PLN") return amount * 0.23;
    if (currency == "TRY") return amount * 0.027;
    if (currency == "AED") return amount * 0.25;
    if (currency == "SAR") return amount * 0.24;
    if (currency == "SGD") return amount * 0.69;
    if (currency == "BRL") return amount * 0.17;
    if (currency == "INR") return amount * 0.011;
    return amount;
}

BooksConnections::BooksConnections(const QDir &workingDir)
{
    m_filePathCsv = workingDir.absoluteFilePath("booksConnections.csv");
    _load();
}

void BooksConnections::_save()
{
    QFile file(m_filePathCsv);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "BooksConnections: Could not open file for writing:" << m_filePathCsv;
        return;
    }
    QTextStream out(&file);
    out << "Id1;Id2\n";
    QSet<QString> written;
    for (auto it = m_id_id.constBegin(); it != m_id_id.constEnd(); ++it) {
        // Prepare a key to avoid duplicates A;B and B;A
        QString k1 = it.key();
        QString k2 = it.value();
        if (k1 > k2) {
            std::swap(k1, k2);
        }
        QString pairKey = k1 + ";" + k2;
        if (!written.contains(pairKey)) {
            out << k1 << ";" << k2 << "\n";
            written.insert(pairKey);
        }
    }
}

void BooksConnections::_load()
{
    m_id_id.clear();
    QFile file(m_filePathCsv);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        // Skip header
        if (!in.atEnd()) {
            in.readLine();
        }
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            QStringList parts = line.split(";");
            if (parts.size() >= 2) {
                QString id1 = parts[0];
                QString id2 = parts[1];
                m_id_id.insert(id1, id2);
                m_id_id.insert(id2, id1);
            }
        }
    }
}

void BooksConnections::tryToConnect(
        QHash<AbstractBooksTable *, QModelIndexList> &table_indexes
        , CurrencyRateManager *currencyRateManager)
{
    // Identify types and order
    // Support multiple tables/rows.
    // Group into Left (Book) and Right (Bank/Self).
    
    QList<std::tuple<AbstractBooksTable*, QModelIndex>> leftItems;
    QList<std::tuple<AbstractBooksTable*, QModelIndex>> rightItems;
    
    // Iterate all inputs
    for (auto it = table_indexes.begin(); it != table_indexes.end(); ++it) {
        AbstractBooksTable* table = it.key();
        QModelIndexList indices = it.value();
        
        // Determine type
        bool isBank = qobject_cast<AbstractBooksTableBank*>(table) != nullptr;
        bool isSelf = qobject_cast<EntrySelfTable*>(table) != nullptr;
        
        if (isBank || isSelf) {
            for (const auto& idx : indices) rightItems.append({table, idx});
        } else {
            // Assume strict Book table (not Bank/Self)
            for (const auto& idx : indices) leftItems.append({table, idx});
        }
    }

    // Bank-to-bank (or self-to-self): all items landed on the right side.
    // Promote the first right item to left so the cartesian-product connection is created.
    if (leftItems.isEmpty() && rightItems.size() >= 2) {
        leftItems.append(rightItems.takeFirst());
    }

    if (leftItems.isEmpty() || rightItems.isEmpty())
    {
        return;
    }

    // We use the first item of Left as the "Target Currency" source?
    // Or we need a reference currency.
    // Usually Book Currency is the reference.
    // If multiple Books have different currencies? That's weird. Assume single currency for Book side usually.
    // But let's take the first Book item's currency as Ref.
    
    auto [firstLeftTable, firstLeftIdx] = leftItems.first();
    QString refCurrency = firstLeftTable->data(firstLeftTable->index(firstLeftIdx.row(), 2)).toString();
    QDate refDate = firstLeftTable->data(firstLeftTable->index(firstLeftIdx.row(), 0)).toDate();
    
    double sumLeft = 0.0;
    double sumRight = 0.0;
    
    // Sum Left
    for (const auto& item : leftItems) {
        auto [tbl, idx] = item;
        double amt = tbl->data(tbl->index(idx.row(), 1)).toDouble();
        QString curr = tbl->data(tbl->index(idx.row(), 2)).toString();
        
        if (curr != refCurrency) {
             if (currencyRateManager) {
                 double r = currencyRateManager->rate(curr, refCurrency, refDate);
                 sumLeft += amt * r;
             } else {
                 ExceptionWithTitleText exception(
                     QObject::tr("Currency Rate Error"),
                     QObject::tr("A currency rate is required to convert %1 to %2 but no rate manager is available.")
                         .arg(curr, refCurrency));
                 exception.raise();
             }
        } else {
            sumLeft += amt;
        }
    }

    // Sum Right (Convert to RefCurrency)
    for (const auto& item : rightItems) {
        auto [tbl, idx] = item;
        double amt = tbl->data(tbl->index(idx.row(), 1)).toDouble();
        QString curr = tbl->data(tbl->index(idx.row(), 2)).toString();

        if (curr != refCurrency) {
             if (currencyRateManager) {
                 double r = currencyRateManager->rate(curr, refCurrency, refDate);
                 sumRight += amt * r;
             } else {
                 ExceptionWithTitleText exception(
                     QObject::tr("Currency Rate Error"),
                     QObject::tr("A currency rate is required to convert %1 to %2 but no rate manager is available.")
                         .arg(curr, refCurrency));
                 exception.raise();
             }
        } else {
            sumRight += amt;
        }
    }
    
    // Check Equality
    double amountDiff = std::abs(sumLeft) - std::abs(sumRight);
    double maxAbs = std::max(std::abs(sumLeft), std::abs(sumRight));
    double tolerance = std::max(0.4, 0.021 * maxAbs);

    if (std::abs(amountDiff) > tolerance) {
        double diffEur = toEurApprox(std::abs(amountDiff), refCurrency);
        QString eurSuffix = (refCurrency != "EUR")
            ? QObject::tr(", ~%1 EUR").arg(diffEur, 0, 'f', 2)
            : QString();
        ExceptionWithTitleText exception(QObject::tr("Book Equality Error"),
            QObject::tr("Amounts do not match: %1 vs %2 (Ref Currency: %3) (Diff %4 > Tol %5%6)")
                                    .arg(sumLeft).arg(sumRight)
                                    .arg(refCurrency)
                                    .arg(amountDiff).arg(tolerance)
                                    .arg(eurSuffix));
        exception.raise();
    }
    
    // Connect Cartesian Product
    for (const auto& lItem : leftItems) {
        auto [lTbl, lIdx] = lItem;
        QString lId = _getId(lTbl->getId(), lTbl->getRowId(lIdx));
        
        for (const auto& rItem : rightItems) {
            auto [rTbl, rIdx] = rItem;
            QString rId = _getId(rTbl->getId(), rTbl->getRowId(rIdx));
            
            if (!m_id_id.contains(lId, rId)) {
                m_id_id.insert(lId, rId);
                m_id_id.insert(rId, lId);
            }
        }
    }
    _save();
}

void BooksConnections::tryToConnect(AbstractBooksTableBank *left
                                    , const QModelIndexList &indexesLeft
                                    , EntrySelfTable *right
                                    , const QModelIndex &indexRight)
{
    for (const auto &indexLeft : indexesLeft)
    {
        disconnect(left, indexLeft);

        QString idLeft = _getId(left->getId(), left->getRowId(indexLeft));
        QString idRight = _getId(right->getId(), right->getRowId(indexRight));

        m_id_id.insert(idLeft, idRight);
        m_id_id.insert(idRight, idLeft);
    }
    _save();
}

void BooksConnections::disconnect(AbstractBooksTable *booksTable, const QModelIndex &index)
{
    const auto &rowId = booksTable->getRowId(index);
    const auto &firstId = _getId(
                booksTable->getId(),
                rowId);
    if (m_id_id.contains(firstId))
    {
        // Get all connected IDs
        QStringList values = m_id_id.values(firstId);
        m_id_id.remove(firstId);
        
        // For each connected ID, remove the backlink to firstId
        for (const QString &v : std::as_const(values)) {
            m_id_id.remove(v, firstId);
        }
        _save();
    }
}

void BooksConnections::disconnect(EntrySelfTable *selfTable, const QModelIndex &index)
{
    const auto &rowId = selfTable->getRowId(index);
    const auto &firstId = _getId(
                selfTable->getId(),
                rowId);
    if (m_id_id.contains(firstId))
    {
        // Get all connected IDs
        QStringList values = m_id_id.values(firstId);
        m_id_id.remove(firstId);
        
        // For each connected ID, remove the backlink to firstId
        for (const QString &v : std::as_const(values)) {
            m_id_id.remove(v, firstId);
        }
        _save();
    }
}

bool BooksConnections::contains(const QString &booksTableId, const QString &rowId) const
{
    const auto &id = _getId(booksTableId, rowId);
    return m_id_id.contains(id);
}

bool BooksConnections::containsSelf(const QString &booksTableId, const QString &rowId) const
{
    const auto &id = _getId(booksTableId, rowId);
    if (!m_id_id.contains(id))
        return false;
    // otherId format is "tableId_rowId"; self-entry always uses "EntrySelfTable" as tableId
    const auto &otherId = m_id_id[id];
    return otherId.startsWith("EntrySelfTable_");
}

void BooksConnections::associateTablesToIds(
        QList<AbstractBooksTable *> bookTables, const EntrySelfTable *selfEntryTable, const CompanyInfosTable *companyInfosTable)
{
    m_cacheId_table.clear();
    m_cacheId_tableSelf.clear();
    m_internalBankAccount = companyInfosTable ? companyInfosTable->getInternalBankAccount() : QStringLiteral("58000");

    // Map Book Tables
    for (const AbstractBooksTable *table : bookTables) {
        int nRows = table->rowCount();
        const auto &tableId = table->getId();
        for (int i=0; i<nRows; ++i)
        {
            const auto &indexRow = table->index(i, 0);
            const auto &rowId = table->getRowId(indexRow);
            const auto &id = _getId(tableId, rowId);
            m_cacheId_table.insert(id, qMakePair(table, i));
        }
    }

    // Map Self Table
    if (selfEntryTable) {
        int nRows = selfEntryTable->rowCount();
        const auto &tableId = selfEntryTable->getId();
        for (int i=0; i<nRows; ++i) {
            const auto &indexRow = selfEntryTable->index(i, 0);
            const auto &rowId = selfEntryTable->getRowId(indexRow);
            const auto &id = _getId(tableId, rowId);
            m_cacheId_tableSelf.insert(id, qMakePair(selfEntryTable, i));
        }
    }
}

QString BooksConnections::getAccount2(AbstractBooksTableBank *tableBank, int row) const
{
    const auto &tableId = tableBank->getId();
    const auto &indexRow = tableBank->index(row, 0);
    const auto &rowId = tableBank->getRowId(indexRow);
    const auto &id = _getId(tableId, rowId);
    const auto &otherId = m_id_id[id];
    if (m_cacheId_table.contains(otherId))
    {
        auto [otherTable, otherRow] = m_cacheId_table[otherId];
        if (qobject_cast<const AbstractBooksTableBank *>(otherTable)) {
            return m_internalBankAccount;
        }
        return otherTable->getAccount2(otherRow);
    }
    else if (m_cacheId_tableSelf.contains(otherId))
    {
        auto [selfTable, selfRow] = m_cacheId_tableSelf[otherId];
        return selfTable->getAccount(selfRow);
    }
    Q_ASSERT(false); // Should not happen
    return "TODOACT2";
}

QDate BooksConnections::getLinkedDate(AbstractBooksTableBank *tableBank, int row) const
{
    const auto &tableId = tableBank->getId();
    const auto &indexRow = tableBank->index(row, 0);
    const auto &rowId = tableBank->getRowId(indexRow);
    const auto &id = _getId(tableId, rowId);
    const auto &otherId = m_id_id[id];
    if (m_cacheId_table.contains(otherId)) {
        auto [otherTable, otherRow] = m_cacheId_table[otherId];
        if (!qobject_cast<const AbstractBooksTableBank *>(otherTable)) {
            return otherTable->getDate(otherRow);
        }
    }
    return {};
}

QString BooksConnections::_getId(const QString &booksTableId, const QString &rowId) const
{
    return booksTableId + "_" + rowId;
}
