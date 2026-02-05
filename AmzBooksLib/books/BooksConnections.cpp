#include <QFile>
#include <QTextStream>
#include <cmath>
#include <QDebug>
#include "AbstractBooksTable.h"
#include "AbstractBooksTableBank.h"
#include "EntrySelfTable.h"
#include "ExceptionBookEquality.h"

#include "CurrencyRateManager.h"

#include "BooksConnections.h"

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
             // Convert Left items to RefCurrency too? 
             // If allow mixed Book currencies.
             if (currencyRateManager) {
                 double r = currencyRateManager->rate(curr, refCurrency, refDate);
                 sumLeft += amt * r;
             } else {
                 // Fallback or error? Assume 1:1
                 sumLeft += amt;
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
                 sumRight += amt;
             }
        } else {
            sumRight += amt;
        }
    }
    
    // Check Equality
    double amountDiff = std::abs(sumLeft) - std::abs(sumRight);
    double maxAbs = std::max(std::abs(sumLeft), std::abs(sumRight));
    double tolerance = std::max(0.005, 0.01 * maxAbs);

    if (std::abs(amountDiff) > tolerance) {
         throw ExceptionBookEquality(QObject::tr("Amounts do not match: %1 vs %2 (Ref Currency: %3) (Diff %4 > Tol %5)")
                                    .arg(sumLeft).arg(sumRight)
                                    .arg(refCurrency)
                                    .arg(amountDiff).arg(tolerance));
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
        for (const QString &v : values) {
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

void BooksConnections::associateTablesToIds(
        QList<const AbstractBooksTable *> bookTables, const EntrySelfTable *selfEntryTable)
{
    m_cacheId_table.clear();
    const auto &selfTableId = selfEntryTable->getId();
    for (const AbstractBooksTable *table : bookTables) {
        int nRows = table->rowCount();
        const auto &tableId = table->getId();
        for (int i=0; i<nRows; ++i)
        {
            const auto &indexRow = table->index(i, 0);
            const auto &rowId = table->getRowId(indexRow);
            const auto &id = _getId(tableId, rowId);
            const auto &otherId = m_id_id[id];
            if (selfTableId.startsWith(selfTableId))
            {
                m_cacheId_tableSelf.insert(id, selfEntryTable);
            }
            else
            {
                m_cacheId_table.insert(id, table);
            }
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
        auto otherTable = m_cacheId_table[otherId];
        return otherTable->getAccount2(row);
    }
    else if (m_cacheId_tableSelf.contains(otherId))
    {
        auto selfTable = m_cacheId_tableSelf[otherId];
        return selfTable->getAccount(row);
    }
    Q_ASSERT(false); // Should not happen
    return QString{};
}

QString BooksConnections::_getId(const QString &booksTableId, const QString &rowId) const
{
    return booksTableId + "_" + rowId;
}
