#include "AbstractBooksTableBank.h"
#include "banks/AbstractBankStatement.h"
#include <QDebug>

AbstractBooksTableBank::AbstractBooksTableBank(const BooksConnections *bookConnections, const QDir &workingDir, QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
    , m_workingDir(workingDir)
{
}

void AbstractBooksTableBank::addFilePaths(const QStringList &filePaths, bool saveToDisk)
{
    const AbstractBankStatement *bank = getBankStatement();
    if (!bank)
    {
        return;
    }

    for (const QString &filePath : filePaths) {
        QSet<QString> doneIds;
        auto rows = bank->readRows(filePath);
        if (rows && !rows->isEmpty()) {
            // Determine year from first row
            int year = rows->first().date.year();
            QString finalPath = filePath;
            
            if (saveToDisk) {
                finalPath = _recordFilePaths(year, filePath);
            }

            for (const auto &row : *rows) {
                // Generate a unique ID based on row content
                QString baseId = QString("%1_%2_%3_%4")
                        .arg(bank->getId(),
                             row.date.toString("yyyyMMdd"),
                             QString::number(row.amount, 'f', 2),
                             QString::number(row.fees, 'f', 2));
                auto id = baseId;
                int i = 1;
                while (doneIds.contains(id))
                {
                    id = baseId += "-" + QString::number(i);
                    ++i;
                }
                doneIds.insert(id);

                bool hasAmount = qAbs(row.amount) > 0.001;
                bool hasFees = qAbs(row.fees) > 0.001;

                if (hasAmount) {
                    add(id, "", row.date, row.amount, row.currency, row.label,
                        bank->defaultAccount(), "", 0.0, "", "");
                    m_rowId_filePath[id] = finalPath;
                }

                if (hasFees) {
                    QString feeId = id + "_fees";
                    QString feeLabel = "Fees " + row.label;
                    add(feeId, "", row.date, row.fees, row.currency, feeLabel,
                        bank->defaultAccount(), bank->defaultAccountFees(), 0.0, "", "");
                    m_rowId_filePath[feeId] = finalPath;
                }
            }
        }
    }
}

QMap<QString, AbstractBooksTableBank::FactoryFunc> &AbstractBooksTableBank::getTables()
{
    static QMap<QString, FactoryFunc> tables;
    return tables;
}

const QMap<QString, AbstractBooksTableBank::FactoryFunc> &AbstractBooksTableBank::ALL_TABLES()
{
    return getTables();
}


void AbstractBooksTableBank::load(int year)
{
    QStringList files = _loadFilePaths(year);
    addFilePaths(files, false);
}

void AbstractBooksTableBank::removeFile(const QString &filePath)
{
    // Find all IDs associated with this file
    QList<QString> idsToRemove;
    for (auto it = m_rowId_filePath.begin(); it != m_rowId_filePath.end(); ++it) {
        if (it.value() == filePath) {
            idsToRemove.append(it.key());
        }
    }
    
    // Remove from table
    for (const QString &id : idsToRemove) {
        AbstractBooksTable::remove(id);
        m_rowId_filePath.remove(id);
    }
    
    // Delete file from disk
    QFile::remove(filePath);
}

void AbstractBooksTableBank::remove(const QList<QModelIndex> &indices)
{
    QSet<QString> filesToRemove;
    for (const QModelIndex &idx : indices) {
        QString id = getRowId(idx);
        if (m_rowId_filePath.contains(id)) {
            filesToRemove.insert(m_rowId_filePath[id]);
        }
    }
    
    for (const QString &filePath : filesToRemove) {
        removeFile(filePath);
    }
}

QString AbstractBooksTableBank::_recordFilePaths(int year, const QString &filePath)
{
    QDir yearDir(m_workingDir.filePath("banks/" + QString::number(year)));
    if (!yearDir.exists())
    {
        yearDir.mkpath(".");
    }

    QDir banksDir(yearDir.filePath(getId()));
    if (!banksDir.exists())
    {
        banksDir.mkpath(".");
    }

    QString fileName = QFileInfo(filePath).fileName();
    QString destPath = banksDir.filePath(fileName);

    // Copy file
    if (filePath != destPath) {
        // If dest exists, maybe remove it first or overwrite?
        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        QFile::copy(filePath, destPath);
    }

    return destPath;
}

QStringList AbstractBooksTableBank::_loadFilePaths(int year)
{
    QDir banksDir(m_workingDir.filePath("banks/" + QString::number(year) + "/" + getId()));

    if (!banksDir.exists())
    {
        return QStringList();
    }

    QStringList files;
    QFileInfoList infoList = banksDir.entryInfoList(QDir::Files);
    for (const QFileInfo &info : infoList) {
        files.append(info.absoluteFilePath());
    }
    return files;
}

AbstractBooksTableBank::Recorder::Recorder(const QString& id, FactoryFunc factory)
{
    getTables()[id] = factory;
}
