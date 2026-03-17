#include <QRegularExpression>
#include <QFileInfo>
#include <QDirIterator>
#include <QCoreApplication> // For tr() if needed, but QObject::tr is better if Q_OBJECT is present, or QCoreApplication::translate

#include "ExceptionWithTitleText.h"

#include "PurchaseInvoiceManager.h"
#include "BookAccountPurchaseTable.h"

// PurchaseInvoiceManager is Q_OBJECT, so we can use tr() inside member functions, but decode is static.
// So we should use QObject::tr() or QCoreApplication::translate.
// Or just QObject::tr() since it inherits QObject (actually QAbstractTableModel).
// Static methods don't have 'this', but can access static metaobject? No, simpler to just use QObject::tr context or QCoreApplication.
// Let's use QObject::tr("PurchaseInvoiceManager", ...) logic or simply QCoreApplication::translate.
// Actually, ExceptionFileError takes strings.


const QStringList PurchaseInvoiceManager::HEADER = {
    "Date", "Account", "Label", "Supplier", "From", "To", "VAT", "Total", "Currency"
};

PurchaseInvoiceManager::PurchaseInvoiceManager(const QDir &workingDir, const QString &companyCountryCode, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
    , m_companyCountryCode(companyCountryCode)
{
    m_purchaseTable = new BookAccountPurchaseTable(workingDir, companyCountryCode, this);
    _load();
}

const BookAccountPurchaseTable *PurchaseInvoiceManager::getPurchaseTable() const
{
    return m_purchaseTable;
}

bool PurchaseInvoiceManager::isSupplierWithCountries(const QString &supplierAccount) const
{
    return m_suppliersWithCountries.contains(supplierAccount);
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
        case 4: return item.countryCodeFrom;
        case 5: return item.countryCodeTo;
        case 6: return item.vatTokens.join(", ");
        case 7: return item.totalAmount;
        case 8: return item.currency;
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

    info.filePath = destFilePath;

    if (info.hasExplicitRoute) {
        m_suppliersWithCountries.insert(info.accountSupplier);
    }

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
    m_suppliersWithCountries.clear();
    
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
        PurchaseInformation info = decode(filePath, m_purchaseTable, m_companyCountryCode);
        // Only add if it looks valid (e.g. has a date)
        if (info.date.isValid()) {
            info.filePath = filePath;
            m_data.append(info);
            
            if (info.hasExplicitRoute) {
                m_suppliersWithCountries.insert(info.accountSupplier);
            }
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

PurchaseInformation PurchaseInvoiceManager::decode(const QString &fileName
                                                   , const BookAccountPurchaseTable *purchaseTable
                                                   , const QString &companyCountryCode)
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
    // Both 2-letter groups must be valid ISO 3166-1 alpha-2 country codes.
    static const QSet<QString> validCountryCodes = {
        // EU
        "AT", "BE", "BG", "CY", "CZ", "DE", "DK", "EE", "ES", "FI",
        "FR", "GR", "HR", "HU", "IE", "IT", "LT", "LU", "LV", "MT",
        "NL", "PL", "PT", "RO", "SE", "SI", "SK",
        // EEA / other European
        "CH", "GB", "IS", "LI", "NO", "TR", "UA", "RU",
        // Major world
        "AU", "BR", "CA", "CN", "IN", "JP", "KR", "MX", "US",
        // Others common in e-commerce
        "AE", "HK", "MA", "SG", "TH", "TW", "VN", "ZA", "PH", "ID",
        // Pseudo-country: European Union as a whole (e.g. Amazon EU consolidation)
        "EU"
    };
    static QRegularExpression regexRoute("([A-Z]{2})([A-Z]{2})$");
    QRegularExpressionMatch matchRoute = regexRoute.match(info.accountSupplier);
    if (matchRoute.hasMatch()
            && matchRoute.capturedStart(1) >= 3   // require ≥3-char prefix before the route
            && validCountryCodes.contains(matchRoute.captured(1))
            && validCountryCodes.contains(matchRoute.captured(2))) {
        info.countryCodeFrom = matchRoute.captured(1);
        info.countryCodeTo = matchRoute.captured(2);
        info.hasExplicitRoute = true;
    }

    // Check for Route in Label (Ends with -XX-YY, e.g. -PH-FR)
    static QRegularExpression regexRouteLabel("-([A-Z]{2})-([A-Z]{2})$");
    QRegularExpressionMatch matchRouteLabel = regexRouteLabel.match(info.label);
    if (matchRouteLabel.hasMatch()
            && validCountryCodes.contains(matchRouteLabel.captured(1))
            && validCountryCodes.contains(matchRouteLabel.captured(2))) {
        info.countryCodeFrom = matchRouteLabel.captured(1);
        info.countryCodeTo = matchRouteLabel.captured(2);
        info.hasExplicitRoute = true;
    }
    if (info.countryCodeTo.isEmpty()) {
        info.countryCodeTo = companyCountryCode; // fallback: hasExplicitRoute stays false
    }
    
    // The last part is Total
    QString totalPart = parts.last();
    // Parse total: "81.6EUR" -> 81.6, "EUR"; "-120.0EUR" -> -120.0, "EUR"
    // The optional leading minus handles purchase refunds with a negative total.
    static QRegularExpression regexTotal("(-?[0-9.]+)([A-Z]*)");
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
    
    // Tracks country+"|"+rateKey for rates that were explicit in the token label
    // (e.g. "FR-TVA20" → "FR|0.2"). Rates computed by deferred division are excluded.
    QSet<QString> explicitRates;

    // Middle parts are VATs or EXTRAs
    static QRegularExpression regexRate("[0-9.]+");
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
        } else if (token.contains('_')) {
            // ── Dual-amount VAT token ──────────────────────────────────────────────
            // Format: "FR-TVA-2.21EUR_9.28PLN" or "FR-TVA--6.30EUR_-5.46GPB"
            // The part before the last '_' is the company-currency half;
            // the part after is the source/invoice-currency amount.
            info.vatTokens.append(token);

            const int underscoreIdx = token.lastIndexOf('_');
            const QString sourceAmountStr = token.mid(underscoreIdx + 1);
            const QString companyHalf     = token.left(underscoreIdx);

            // Source amount (invoice currency)
            double sourceAmount = 0.0;
            QRegularExpressionMatch mSrc = regexTotal.match(sourceAmountStr);
            if (mSrc.hasMatch()) {
                sourceAmount = qAbs(mSrc.captured(1).toDouble());
            }

            // Company half follows standard VAT token format
            const QStringList halfParts = companyHalf.split('-');
            if (halfParts.size() >= 3) {
                const QString country = halfParts[0];
                const QString label   = halfParts[1];

                double companyAmount = 0.0;
                QRegularExpressionMatch mComp = regexTotal.match(halfParts.last());
                if (mComp.hasMatch()) {
                    companyAmount = qAbs(mComp.captured(1).toDouble());
                }

                // Extract rate from label ("TVA20" → 0.2, "TVA" → "")
                QRegularExpressionMatch matchRate = regexRate.match(label);
                QString rateKey;
                if (matchRate.hasMatch()) {
                    const double rateValPct = matchRate.captured(0).toDouble();
                    rateKey = QString::number(rateValPct / 100.0);
                    explicitRates.insert(country + "|" + rateKey);
                }

                info.country_vatRate_vat[country][rateKey]           += sourceAmount;
                info.country_vatRate_vatCompany[country][rateKey]    += companyAmount;
            }
        } else {
            info.vatTokens.append(token);

            // ── Single-amount VAT token ────────────────────────────────────────────
            // Parse Token: COUNTRY-LABEL-AMOUNT (e.g. FR-TVA5.5-13.6EUR or FR-TVA-13.6EUR)
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
                QRegularExpressionMatch matchRate = regexRate.match(label);
                QString rateKey; // Default empty
                if (matchRate.hasMatch()) {
                    double rateValPercentage = matchRate.captured(0).toDouble();
                    // Store as proportion
                    rateKey = QString::number(rateValPercentage / 100.0);
                    explicitRates.insert(country + "|" + rateKey);
                }

                // If rateKey is missing, defer rate computation until after the loop
                // (we need all VAT amounts first to derive the untaxed base).
                info.country_vatRate_vat[country][rateKey] += vatAmount;
            }
        }
    }
    
    // Defer VAT rate calculation for empty rateKeys now that we have all VATs
    double totalVatDeferred = 0.0;
    for (const auto &countryRates : info.country_vatRate_vat) {
        for (double amt : countryRates.values()) {
            totalVatDeferred += amt;
        }
    }
    
    // Compute total extra (subUntaxedAmount) to subtract from the taxable base
    double totalExtraDeferred = 0.0;
    for (double amt : info.subUntaxedAmount.values()) {
        totalExtraDeferred += amt;
    }

    double untaxedAmount = qAbs(info.totalAmount) - totalVatDeferred - totalExtraDeferred;

    // Now go through and fix empty VAT rates
    for (auto itCountry = info.country_vatRate_vat.begin(); itCountry != info.country_vatRate_vat.end(); ++itCountry) {
        if (itCountry.value().contains("")) {
            double vatAmount = itCountry.value().take("");
            if (untaxedAmount > 0.001) {
                double calculatedRate = (vatAmount / untaxedAmount) * 100.0;
                // Round to 1 decimal place to match typical rates (e.g. 5.5, 20.0), then convert to proportion
                double roundedRate = qRound(calculatedRate * 10.0) / 10.0;
                QString newRateKey = QString::number(roundedRate / 100.0);
                itCountry.value()[newRateKey] += vatAmount;
                // Sync the companion company-currency map for dual-amount tokens
                if (info.country_vatRate_vatCompany.contains(itCountry.key())) {
                    auto &companyMap = info.country_vatRate_vatCompany[itCountry.key()];
                    if (companyMap.contains("")) {
                        const double companyVatAmount = companyMap.take("");
                        companyMap[newRateKey] += companyVatAmount;
                    }
                }
            } else {
                // Fallback to 0 if untaxed is 0 (should not happen normally)
                itCountry.value()["0"] += vatAmount;
            }
        }
    }
    
    // Derive simple rawVatAmount / vatCurrency / vatCountry from the parsed tokens
    if (!info.vatTokens.isEmpty()) {
        double totalVat = 0.0;
        QString firstVatCurrency;
        for (const QString &token : std::as_const(info.vatTokens)) {
            // For dual-amount tokens the source/invoice-currency amount is after the '_'.
            // For single-amount tokens use the last '-'-split part as before.
            const bool isDualAmount = token.contains('_');
            QString amountPart;
            bool negativeVat = false;
            if (isDualAmount) {
                // Source amount may start with '-'; sign is embedded in the number.
                amountPart = token.mid(token.lastIndexOf('_') + 1);
            } else {
                const QStringList tokenSplits = token.split('-');
                amountPart = tokenSplits.last();
                // A double-dash (--XAMOUNT) leaves an empty string at [size-2],
                // meaning the numeric value is negative.
                negativeVat = tokenSplits.size() >= 2
                              && tokenSplits[tokenSplits.size() - 2].isEmpty();
            }
            QRegularExpressionMatch m = regexTotal.match(amountPart);
            if (m.hasMatch()) {
                double amt = m.captured(1).toDouble();
                if (negativeVat) {
                    amt = -amt;
                }
                totalVat += amt;
                if (firstVatCurrency.isEmpty()) {
                    firstVatCurrency = m.captured(2);
                }
            }
        }
        if (totalVat != 0.0) {
            info.rawVatAmount = QString::number(totalVat);
        }
        info.vatCurrency = firstVatCurrency;

        // Extract country code from the first token's leading part (e.g. "FR" in "FR-TVA20-13.6EUR")
        const QStringList firstParts = info.vatTokens.first().split('-');
        if (firstParts.size() >= 3)
            info.vatCountry = firstParts[0];
    }
    
    // Validate VAT accounts for explicitly-specified rates only.
    // Rates that were deferred-computed (no rate number in the token label, e.g. "FR-TVA")
    // are excluded because the computed value may differ slightly from the stored rate;
    // JournalEntryFactory handles those with a closest-match tolerance.
    // Also skip when vatCurrency differs from invoice currency: the rate stored in the map
    // was computed without currency conversion and may be unreliable.
    if (purchaseTable && (info.vatCurrency.isEmpty() || info.vatCurrency == info.currency)) {
        for (auto itCountry = info.country_vatRate_vat.constBegin(); itCountry != info.country_vatRate_vat.constEnd(); ++itCountry) {
            for (auto itRate = itCountry.value().constBegin(); itRate != itCountry.value().constEnd(); ++itRate) {
                if (!explicitRates.contains(itCountry.key() + "|" + itRate.key())) {
                    continue;
                }
                double rate = itRate.key().toDouble();
                // This validates the configuration and throws ExceptionWithTitleText if it doesn't exist
                purchaseTable->getAccountsDebit6(info.countryCodeTo, rate);
            }
        }
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

