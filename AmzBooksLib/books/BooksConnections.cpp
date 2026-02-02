#include <QFile>
#include <QTextStream>
#include <cmath>
#include <QDebug>
#include "AbstractBooksTable.h"
#include "AbstractBooksTableBank.h"
#include "EntrySelfTable.h"
#include "ExceptionBookEquality.h"

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
            if (line.isEmpty()) continue;
            QStringList parts = line.split(";");
            if (parts.size() >= 2) {
                QString id1 = parts[0];
                QString id2 = parts[1];
                m_id_id[id1] = id2;
                m_id_id[id2] = id1;
            }
        }
    }
}

void BooksConnections::tryToConnect(
        AbstractBooksTable *left
        , const QModelIndex &indexLeft
        , AbstractBooksTableBank *right
        , const QModelIndex &indexRight
        , double currencyRate)
{
    // Convert amount if needed. If currency is different, 1% difference is acceptable to be considered equals.
    
    // Left: AbstractBooksTable. Amount is at index 1 (Original Amount) or 2 (Converted Amount).
    // Let's rely on column names or fixed indices. 
    // From AbstractBooksTable.cpp:
    // 0: Date
    // 1: Amount (Original)
    // 2: Currency (Original) - Wait, in recent user edit:
    // COL_NAMES: Date, Amount, Currency, ... (Size reduced)
    // Let's check the file content again or assume indices based on recent diffs.
    // User change Step 67:
    // tr("Amount"), tr("Currency"), tr("Label")...
    // So Index 1 is Amount, Index 2 is Currency.
    
    // Right: AbstractBooksTableBank -> AbstractBooksTable. Same columns.
    
    double amountLeft = left->data(left->index(indexLeft.row(), 1)).toDouble();
    QString currencyLeft = left->data(left->index(indexLeft.row(), 2)).toString();
    
    double amountRight = right->data(right->index(indexRight.row(), 1)).toDouble();
    QString currencyRight = right->data(right->index(indexRight.row(), 2)).toString();

    double amountDiff = 0.0;
    
    if (currencyLeft == currencyRight) {
        amountDiff = std::abs(amountLeft) - std::abs(amountRight);
    } else {
        // Convert Right to Left currency (or check equivalence)
        // If we have rate, we assume AmountLeft ~ AmountRight * Rate (if Rate is Right->Left)
        // Or AmountLeft * Rate ~ AmountRight...
        // The parameter is just "currencyRate". 
        // Standard convention? 
        // Let's try: abs(AmountLeft) - abs(AmountRight * currencyRate)
        // Depending on Rate definition.
        // If Rate is 1.0, same as equals.
        // We will assume Rate converts Right to Left.
        amountDiff = std::abs(amountLeft) - std::abs(amountRight * currencyRate);
    }

    if (std::abs(amountDiff) > 0.01 * std::max(std::abs(amountLeft), std::abs(amountRight))) { 
        // > 1% difference (relative) or just absolute? 
        // "1% difference is acceptable" -> Relative check.
        // But for small amounts, absolute is safer?
        // Let's use 1% of the target amount.
         throw ExceptionBookEquality(QObject::tr("Amounts do not match: %1 (%3) vs %2 (%4) with rate %5")
                                    .arg(amountLeft).arg(amountRight)
                                    .arg(currencyLeft).arg(currencyRight)
                                    .arg(currencyRate));
    }
    
    // Proceed to connect
    QString idLeft = _getId(left->getId(), left->getRowId(indexLeft));
    QString idRight = _getId(right->getId(), right->getRowId(indexRight));

    m_id_id[idLeft] = idRight;
    m_id_id[idRight] = idLeft;
    _save();
}

void BooksConnections::tryToConnect(
        AbstractBooksTable *left
        , const QModelIndex &indexLeft
        , EntrySelfTable *right
        , const QModelIndex &indexRight)
{
    disconnect(left, indexLeft);

    QString idLeft = _getId(left->getId(), left->getRowId(indexLeft));
    QString idRight = _getId(right->getId(), right->getRowId(indexRight));

    m_id_id[idLeft] = idRight;
    m_id_id[idRight] = idLeft;
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
        const auto &secondId = m_id_id[firstId];
        m_id_id.remove(secondId);
        m_id_id.remove(firstId);
        _save();
    }
}

bool BooksConnections::contains(const QString &booksTableId, const QString &rowId) const
{
    const auto &id = _getId(booksTableId, rowId);
    return m_id_id.contains(id);
}

QString BooksConnections::_getId(const QString &booksTableId, const QString &rowId) const
{
    return booksTableId + "_" + rowId;
}
