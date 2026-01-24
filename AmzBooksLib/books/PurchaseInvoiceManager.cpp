#include <QRegularExpression>
#include <QFileInfo>
#include <QDirIterator>
#include <QCoreApplication> // For tr() if needed, but QObject::tr is better if Q_OBJECT is present, or QCoreApplication::translate

#include "ExceptionFileError.h"

#include "PurchaseInvoiceManager.h"

// PurchaseInvoiceManager is Q_OBJECT, so we can use tr() inside member functions, but decode is static.
// So we should use QObject::tr() or QCoreApplication::translate.
// Or just QObject::tr() since it inherits QObject (actually QAbstractTableModel).
// Static methods don't have 'this', but can access static metaobject? No, simpler to just use QObject::tr context or QCoreApplication.
// Let's use QObject::tr("PurchaseInvoiceManager", ...) logic or simply QCoreApplication::translate.
// Actually, ExceptionFileError takes strings.


const QStringList PurchaseInvoiceManager::HEADER = {
    "Date", "Account", "Label", "Supplier", "VAT", "Total", "Currency"
};

PurchaseInvoiceManager::PurchaseInvoiceManager(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    _load();
}

int PurchaseInvoiceManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_data.size();
}

int PurchaseInvoiceManager::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return HEADER.size();
}

QVariant PurchaseInvoiceManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size())
        return QVariant();

    const auto &item = m_data.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return item.date;
        case 1: return item.account;
        case 2: return item.label;
        case 3: return item.supplier;
        case 4: return item.vatTokens.join(", ");
        case 5: return item.totalAmount;
        case 6: return item.currency;
        }
    }
    
    return QVariant();
}

QVariant PurchaseInvoiceManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section < HEADER.size())
            return HEADER[section];
    }
    return QVariant();
}

void PurchaseInvoiceManager::add(const QString &sourceFilePath, PurchaseInformation &info)
{
    QFileInfo sourceInfo(sourceFilePath);
    if (!sourceInfo.exists()) {
        throw ExceptionFileError(QObject::tr("File Not Found"),
                                 QObject::tr("The source file '%1' does not exist.").arg(sourceFilePath));
    }
    
    if (info.originalExtension.isEmpty()) {
        info.originalExtension = sourceInfo.suffix();
    }
    
    QString relativePath = getRelativePath(info);
    QDir destDir(m_workingDir);
    if (!destDir.mkpath(relativePath)) {
        throw ExceptionFileError(QObject::tr("Directory Error"),
                                 QObject::tr("Could not create directory '%1'.").arg(relativePath));
    }
    
    if (!destDir.cd(relativePath)) {
         throw ExceptionFileError(QObject::tr("Directory Error"),
                                 QObject::tr("Could not access directory '%1'.").arg(relativePath));
    }
    
    QString fileName = encode(info);
    QString destFilePath = destDir.filePath(fileName);
    
    if (!QFile::copy(sourceFilePath, destFilePath)) {
        throw ExceptionFileError(QObject::tr("Copy Error"),
                                 QObject::tr("Failed to copy file from '%1' to '%2'.").arg(sourceFilePath, destFilePath));
    }
    
    // Refresh model
    _load();
}

void PurchaseInvoiceManager::_load()
{
    beginResetModel();
    m_data.clear();
    
    QDir invoiceDir(m_workingDir);
    if (invoiceDir.cd("purchase-invoices")) {
        scanDirectory(invoiceDir);
    }
    
    // Sort by date descending
    std::sort(m_data.begin(), m_data.end(), [](const PurchaseInformation &a, const PurchaseInformation &b) {
        return a.date > b.date;
    });
    
    endResetModel();
}

void PurchaseInvoiceManager::scanDirectory(const QDir &dir)
{
    // Recursive scan using QDirIterator
    QDirIterator it(dir.path(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        PurchaseInformation info = decode(filePath);
        // Only add if it looks valid (e.g. has a date)
        if (info.date.isValid()) {
            info.filePath = filePath;
            m_data.append(info);
        }
    }
}

PurchaseInformation PurchaseInvoiceManager::decode(const QString &fileName)
{
    PurchaseInformation info;
    QFileInfo fileInfo(fileName);
    info.originalExtension = fileInfo.suffix();
    
    // Remove extension for parsing
    QString baseName = fileInfo.completeBaseName();
    
    // Split by "__"
    QStringList parts = baseName.split("__");
    
    if (parts.size() < 5) {
        throw ExceptionFileError(QObject::tr("Invalid Filename"),
                                 QObject::tr("The filename '%1' does not have enough parts (expected at least 5).").arg(fileName));
    }
    
    info.date = QDate::fromString(parts[0], Qt::ISODate);
    if (!info.date.isValid()) {
        throw ExceptionFileError(QObject::tr("Invalid Date"),
                                 QObject::tr("The date '%1' in filename '%2' is invalid.").arg(parts[0], fileName));
    }

    info.account = parts[1];
    info.label = parts[2];
    info.supplier = parts[3];
    
    // The last part is Total
    QString totalPart = parts.last();
    // Parse total: "81.6EUR" -> 81.6 and EUR
    // Regex for number and letters
    static QRegularExpression regexTotal("([0-9.]+)([A-Z]+)");
    QRegularExpressionMatch match = regexTotal.match(totalPart);
    if (match.hasMatch()) {
        QString amountStr = match.captured(1);
        info.totalAmount = amountStr.toDouble();
        info.rawTotalAmount = amountStr;
        info.currency = match.captured(2);
    } else {
        // Fallback
        info.totalAmount = totalPart.toDouble();
        info.rawTotalAmount = totalPart;
    }
    
    // Middle parts are VATs
    for (int i = 4; i < parts.size() - 1; ++i) {
        info.vatTokens.append(parts[i]);
    }
    
    return info;
}

QString PurchaseInvoiceManager::encode(const PurchaseInformation &info)
{
    QStringList parts;
    parts << info.date.toString(Qt::ISODate);
    parts << info.account;
    parts << info.label;
    parts << info.supplier;
    
    parts.append(info.vatTokens);
    
    // Total formatting
    QString totalStr;
    if (!info.rawTotalAmount.isEmpty()) {
        totalStr = info.rawTotalAmount;
    } else {
        totalStr = QString::number(info.totalAmount);
    }
    
    if (!info.currency.isEmpty()) {
        totalStr += info.currency;
    }
    parts << totalStr;
    
    QString fileName = parts.join("__");
    if (!info.originalExtension.isEmpty()) {
        fileName += "." + info.originalExtension;
    }
    
    return fileName;
}

QString PurchaseInvoiceManager::getRelativePath(const PurchaseInformation &info)
{
    return QString("purchase-invoices/%1/%2")
            .arg(info.date.year())
            .arg(info.date.month(), 2, 10, QChar('0'));
}
