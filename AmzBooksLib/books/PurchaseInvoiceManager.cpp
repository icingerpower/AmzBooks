#include <QRegularExpression>
#include <QFileInfo>
#include <QDirIterator>
#include <QCoreApplication> // For tr() if needed, but QObject::tr is better if Q_OBJECT is present, or QCoreApplication::translate

#include "ExceptionWithTitleText.h"

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
        case 3: return item.accountSupplier;
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
        ExceptionWithTitleText exception(QObject::tr("File Not Found"),
                                 QObject::tr("The source file '%1' does not exist.").arg(sourceFilePath));
        exception.raise();
    }
    
    if (info.originalExtension.isEmpty()) {
        info.originalExtension = sourceInfo.suffix();
    }
    
    QString relativePath = getRelativePath(info);
    QDir destDir(m_workingDir);
    if (!destDir.mkpath(relativePath)) {
        ExceptionWithTitleText exception(QObject::tr("Directory Error"),
                                 QObject::tr("Could not create directory '%1'.").arg(relativePath));
        exception.raise();
    }
    
    if (!destDir.cd(relativePath)) {
         ExceptionWithTitleText exception(QObject::tr("Directory Error"),
                                 QObject::tr("Could not access directory '%1'.").arg(relativePath));
         exception.raise();
    }
    
    QString fileName = encode(info);
    QString destFilePath = destDir.filePath(fileName);
    
    if (!QFile::copy(sourceFilePath, destFilePath)) {
        ExceptionWithTitleText exception(QObject::tr("Copy Error"),
                                 QObject::tr("Failed to copy file from '%1' to '%2'.").arg(sourceFilePath, destFilePath));
        exception.raise();
    }
    
    // Refresh model
    _load();
    // Refresh model
    _load();
}

bool PurchaseInvoiceManager::remove(const QString &fileName)
{
    // Search in current data to find the file path
    QString filePath;
    for (const auto &info : m_data) {
        QFileInfo fi(info.filePath);
        if (fi.fileName() == fileName) {
            filePath = info.filePath;
            break;
        }
    }

    if (filePath.isEmpty()) {
        // Maybe the user passed the full path?
        if (QFile::exists(fileName)) {
            filePath = fileName;
        } else {
            return false;
        }
    }

    if (!QFile::remove(filePath)) {
        return false;
    }

    _load();
    return true;
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

QList<PurchaseInformation> PurchaseInvoiceManager::getInvoices(const QDate &from, const QDate &to) const
{
    QList<PurchaseInformation> result;
    for (const auto &info : m_data) {
        if (info.date >= from && info.date <= to) {
            result.append(info);
        }
    }
    return result;
}

PurchaseInformation PurchaseInvoiceManager::decode(const QString &fileName)
{
    PurchaseInformation info;
    QFileInfo fileInfo(fileName);
    info.originalExtension = fileInfo.suffix();
    
    // Global checks on the full filename (or base name)
    QString baseName = fileInfo.completeBaseName();

    // Use a space-stripped copy only for flag detection so that labels with
    // spaces (e.g. "stock DDP") still round-trip correctly through encode().
    const QString baseNameNoSpaces = QString(baseName).replace(" ", "");

    if (baseNameNoSpaces.contains("stock", Qt::CaseInsensitive)) {
        info.isInventory = true;
    }

    if (baseNameNoSpaces.contains("ddp", Qt::CaseInsensitive)) {
        info.isDDP = true;
    }

    // Split by "__"
    QStringList parts = baseName.split("__");
    
    if (parts.size() < 5) {
        ExceptionWithTitleText exception(QObject::tr("Invalid Filename"),
                                 QObject::tr("The filename '%1' does not have enough parts (expected at least 5).").arg(fileName));
        exception.raise();
    }
    
    info.date = QDate::fromString(parts[0], Qt::ISODate);
    if (!info.date.isValid()) {
        ExceptionWithTitleText exception(QObject::tr("Invalid Date"),
                                 QObject::tr("The date '%1' in filename '%2' is invalid.").arg(parts[0], fileName));
        exception.raise();
    }

    info.account = parts[1];
    info.label = parts[2];
    info.accountSupplier = parts[3];
    
    // Check for Route in Supplier (Ends with 4 caps, e.g. CNFR)
    static QRegularExpression regexRoute("([A-Z]{2})([A-Z]{2})$");
    QRegularExpressionMatch matchRoute = regexRoute.match(info.accountSupplier);
    if (matchRoute.hasMatch()) {
        info.countryCodeFrom = matchRoute.captured(1);
        info.countryCodeTo = matchRoute.captured(2);
    }
    
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
    
    // Middle parts are VATs or EXTRAs
    for (int i = 4; i < parts.size() - 1; ++i) {
        QString token = parts[i];
        
        if (token.startsWith("EXTRA-")) {
            // Parse EXTRA-ACCOUNT-AMOUNT (e.g. EXTRA-607222-20.1EUR)
            QStringList extraParts = token.split('-');
            if (extraParts.length() >= 3) {
                 QString account = extraParts[1];
                 QString amountStr = extraParts.last(); 
                 
                 double extraAmount = 0.0;
                 QRegularExpressionMatch matchAmt = regexTotal.match(amountStr);
                 if (matchAmt.hasMatch()) {
                     extraAmount = matchAmt.captured(1).toDouble();
                 } else {
                     extraAmount = amountStr.toDouble();
                 }
                 
                 info.subUntaxedAmount[account] += extraAmount;
            }
        } else {
            info.vatTokens.append(token);
            
            // Parse Token: COUNTRY-LABEL-AMOUNT (e.g. FR-TVA5.5-13.6EUR or FR-TVA-13.6EUR)
            // Split by '-'
            QStringList tokenParts = token.split('-');
            if (tokenParts.size() >= 3) {
                QString country = tokenParts[0];
                QString label = tokenParts[1];
                QString amountStrWithCurr = tokenParts.last(); // Last part is amount
                
                // Extract numeric amount from amountStrWithCurr (e.g. 13.6EUR -> 13.6)
                double vatAmount = 0.0;
                QRegularExpressionMatch matchAmt = regexTotal.match(amountStrWithCurr);
                if (matchAmt.hasMatch()) {
                    vatAmount = matchAmt.captured(1).toDouble();
                } else {
                     vatAmount = amountStrWithCurr.toDouble();
                }
                
                // Extract Rate from Label (e.g. "TVA5.5" -> "5.50", "TVA" -> "")
                // Look for digits in label
                static QRegularExpression regexRate("[0-9.]+");
                QRegularExpressionMatch matchRate = regexRate.match(label);
                QString rateKey = ""; // Default empty
                if (matchRate.hasMatch()) {
                    double rateVal = matchRate.captured(0).toDouble();
                    // Format with 2 decimal places
                    rateKey = QString::number(rateVal, 'f', 2);
                }
                
                info.country_vatRate_vat[country][rateKey] += vatAmount;
            }
        }
    }
    
    // Derive simple rawVatAmount / vatCurrency / vatCountry from the parsed tokens
    if (!info.vatTokens.isEmpty()) {
        double totalVat = 0.0;
        QString firstVatCurrency;
        for (const QString &token : std::as_const(info.vatTokens)) {
            const QString amountPart = token.split('-').last();
            QRegularExpressionMatch m = regexTotal.match(amountPart);
            if (m.hasMatch()) {
                totalVat += m.captured(1).toDouble();
                if (firstVatCurrency.isEmpty())
                    firstVatCurrency = m.captured(2);
            }
        }
        if (totalVat > 0.0)
            info.rawVatAmount = QString::number(totalVat);
        info.vatCurrency = firstVatCurrency;

        // Extract country code from the first token's leading part (e.g. "FR" in "FR-TVA20-13.6EUR")
        const QStringList firstParts = info.vatTokens.first().split('-');
        if (firstParts.size() >= 3)
            info.vatCountry = firstParts[0];
    }

    return info;
}

QString PurchaseInvoiceManager::encode(const PurchaseInformation &info)
{
    QStringList parts;
    parts << info.date.toString(Qt::ISODate);
    parts << info.account;
    parts << info.label;
    parts << info.accountSupplier;
    
    parts.append(info.vatTokens);
    
    // Add EXTRA tokens from subUntaxedAmount if not already in vatTokens (vatTokens usually stores original strings)
    // But since we split them in decode, we should reconstruct them if we want to support round-trip for synthesized info.
    // However, decode() puts non-EXTRA tokens into vatTokens.
    // So we just append extras here.
    for (auto it = info.subUntaxedAmount.constBegin(); it != info.subUntaxedAmount.constEnd(); ++it) {
        QString amountStr = QString::number(it.value()); 
        // We need to append currency if available in raw? Prefer standard format.
        if (!info.currency.isEmpty()) {
            amountStr += info.currency;
        }
        parts << QString("EXTRA-%1-%2").arg(it.key(), amountStr);
    }
    
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
