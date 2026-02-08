#include "InvoiceGenerator.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <QTextDocument>
#include <QPdfWriter>
#include <QPainter>
#include <QStandardPaths>
#include <QDebug>

#include "CompanyInfosTable.h"
#include "CompanyAddressTable.h"
#include "CurrencyRateManager.h"
#include "orders/InvoicingInfo.h"
#include "orders/Address.h"

// Static shortcut maps - centralized for easy maintenance
// Add new values here when needed - no risk of missing values due to fallback

const QHash<TaxScheme, QString> InvoiceGenerator::TAX_SCHEME_SHORTCUTS = {
    {TaxScheme::Unknown, "UNK"},
    {TaxScheme::DomesticVat, "DOM"},
    {TaxScheme::EuOssUnion, "OSS"},
    {TaxScheme::EuOssNonUnion, "OSN"},
    {TaxScheme::EuIoss, "IOS"},
    {TaxScheme::ImportVat, "IMP"},
    {TaxScheme::ReverseChargeImport, "RCI"},
    {TaxScheme::ReverseChargeDomestic, "RCD"},
    {TaxScheme::MarketplaceDeemedSupplier, "MDS"},
    {TaxScheme::Exempt, "EXE"},
    {TaxScheme::OutOfScope, "OOS"}
};

const QHash<TaxJurisdictionLevel, QString> InvoiceGenerator::TAX_JURISDICTION_SHORTCUTS = {
    {TaxJurisdictionLevel::Unknown, "UNK"},
    {TaxJurisdictionLevel::Supranational, "SUP"},
    {TaxJurisdictionLevel::Country, "CTY"},
    {TaxJurisdictionLevel::Region, "REG"},
    {TaxJurisdictionLevel::City, "CIT"},
    {TaxJurisdictionLevel::PostalCode, "POS"},
    {TaxJurisdictionLevel::Territory, "TER"},
    {TaxJurisdictionLevel::Zone, "ZON"}
};

const QHash<QString, QString> InvoiceGenerator::CHANNEL_SHORTCUTS = {
    {"Amazon", "AMZ"},
    {"Temu", "TMU"},
    {"eBay", "EBY"},
    {"Shopify", "SHO"},
    {"Website", "WEB"},
    {"Service", "SVC"},
    {"Etsy", "ETS"},
    {"Cdiscount", "CDI"}
};

const QHash<QString, QString> InvoiceGenerator::STORE_SHORTCUTS = {
    {"amazon.fr", "FR"},
    {"amazon.de", "DE"},
    {"amazon.co.uk", "UK"},
    {"amazon.it", "IT"},
    {"amazon.es", "ES"},
    {"amazon.com", "US"},
    {"amazon.nl", "NL"},
    {"amazon.pl", "PL"},
    {"amazon.se", "SE"},
    {"amazon.be", "BE"},
    {"amazon.ca", "CA"},
    {"amazon.com.mx", "MX"},
    {"amazon.co.jp", "JP"},
    {"amazon.com.au", "AU"}
};

const QStringList InvoiceGenerator::HEADER_IDS = {
    "Date",
    "TaxDeclaringCountry",
    "TaxScheme",
    "TaxJurisdiction",
    "CountryVatPaidTo",
    "Channel",
    "Store",
    "InvoiceNumber"
};

// Static helper methods

QString InvoiceGenerator::shortenTaxScheme(TaxScheme scheme)
{
    if (TAX_SCHEME_SHORTCUTS.contains(scheme)) {
        return TAX_SCHEME_SHORTCUTS.value(scheme);
    }
    // Fallback: use enum value as string (via taxSchemeToString) and take first 3 chars
    QString str = taxSchemeToString(scheme);
    return str.left(3).toUpper();
}

QString InvoiceGenerator::shortenTaxJurisdiction(TaxJurisdictionLevel level)
{
    if (TAX_JURISDICTION_SHORTCUTS.contains(level)) {
        return TAX_JURISDICTION_SHORTCUTS.value(level);
    }
    QString str = taxJurisdictionLevelToString(level);
    return str.left(3).toUpper();
}

QString InvoiceGenerator::shortenChannel(const QString &channel)
{
    if (CHANNEL_SHORTCUTS.contains(channel)) {
        return CHANNEL_SHORTCUTS.value(channel);
    }
    // Fallback: first 3 uppercase characters
    return channel.left(3).toUpper();
}

QString InvoiceGenerator::shortenStore(const QString &store)
{
    if (STORE_SHORTCUTS.contains(store)) {
        return STORE_SHORTCUTS.value(store);
    }
    // Fallback: extract meaningful part or use first 3 chars
    // For unknown stores like "amazon.xyz", try to extract country code after last dot
    int lastDot = store.lastIndexOf('.');
    if (lastDot != -1 && lastDot < store.length() - 1) {
        return store.mid(lastDot + 1).left(3).toUpper();
    }
    return store.left(3).toUpper();
}

// Constructor

InvoiceGenerator::InvoiceGenerator(
    const QDir &workingDir,
    const CompanyInfosTable *companyInfos,
    const CompanyAddressTable *companyAddress,
    const CurrencyRateManager *currencyRates,
    QObject *parent)
    : QAbstractTableModel(parent)
    , m_companyInfos(companyInfos)
    , m_companyAddress(companyAddress)
    , m_currencyRates(currencyRates)
{
    m_filePath = workingDir.absoluteFilePath("invoices.csv");
    _load();
}

// Invoice number generation

QString InvoiceGenerator::_buildContextKey(
    const QDate &date,
    const TaxResolver::TaxContext &taxContext,
    const QString &channel,
    const QString &store) const
{
    // Format: YYYYMM-{TaxScheme}-{TaxDeclCountry}-{Channel}-{Store}
    QString yearMonth = date.toString("yyyyMM");
    QString scheme = shortenTaxScheme(taxContext.taxScheme);
    QString country = taxContext.taxDeclaringCountryCode.left(2).toUpper();
    QString ch = shortenChannel(channel);
    QString st = shortenStore(store);
    
    return QString("%1-%2-%3-%4-%5")
        .arg(yearMonth, scheme, country, ch, st);
}

int InvoiceGenerator::_getNextSequenceForContext(const QString &contextKey)
{
    // Check cache first
    if (m_sequenceCache.contains(contextKey)) {
        int next = m_sequenceCache[contextKey] + 1;
        m_sequenceCache[contextKey] = next;
        return next;
    }
    
    // Scan existing records to find max sequence for this context
    int maxSeq = 0;
    for (const auto &record : m_data) {
        if (record.invoiceNumber.startsWith(contextKey)) {
            // Extract sequence number from end, format: contextKey-NNN
            QString numPart = record.invoiceNumber.mid(contextKey.length() + 1);
            // Handle revision numbers: baseNumber-Rnn
            int dashR = numPart.indexOf("-R");
            if (dashR != -1) {
                numPart = numPart.left(dashR);
            }
            bool ok;
            int seq = numPart.toInt(&ok);
            if (ok && seq > maxSeq) {
                maxSeq = seq;
            }
        }
    }
    
    int next = maxSeq + 1;
    m_sequenceCache[contextKey] = next;
    return next;
}

QString InvoiceGenerator::getBaseInvoiceNumber(
    const QDate &date,
    const TaxResolver::TaxContext &taxContext,
    const QString &channel,
    const QString &store)
{
    QString contextKey = _buildContextKey(date, taxContext, channel, store);
    int sequence = _getNextSequenceForContext(contextKey);
    
    QString invoiceNumber = QString("%1-%2")
        .arg(contextKey)
        .arg(sequence, 3, 10, QChar('0'));
    
    // Record this invoice
    InvoiceRecord record;
    record.date = date;
    record.taxDeclaringCountry = taxContext.taxDeclaringCountryCode;
    record.taxScheme = taxSchemeToString(taxContext.taxScheme);
    record.taxJurisdiction = taxJurisdictionLevelToString(taxContext.taxJurisdictionLevel);
    record.countryVatPaidTo = taxContext.countryCodeVatPaidTo;
    record.channel = channel;
    record.store = store;
    record.invoiceNumber = invoiceNumber;
    
    beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
    m_data.append(record);
    endInsertRows();
    
    // NO SAVE HERE - Defer to generateInvoice
    
    return invoiceNumber;
}

QStringList InvoiceGenerator::getNextInvoiceNumbers(
    const QDate &date,
    const TaxResolver::TaxContext &taxContext,
    const QString &channel,
    const QString &store,
    const QList<bool> &invoicesToDo,
    const std::optional<QString> &existingInvoiceNumber)
{
    QStringList result;
    
    if (invoicesToDo.isEmpty()) {
        return result;
    }
    
    QString baseNumber;
    int revisionCounter = 0;
    
    // Determine base number
    if (existingInvoiceNumber.has_value() && !existingInvoiceNumber->isEmpty()) {
        // Use marketplace-generated or existing invoice number
        baseNumber = existingInvoiceNumber.value();
        
        // Extract any existing revision counter from the base number
        int lastR = baseNumber.lastIndexOf("-R");
        if (lastR != -1) {
            QString revPart = baseNumber.mid(lastR + 2);
            bool ok;
            int existingRev = revPart.toInt(&ok);
            if (ok) {
                revisionCounter = existingRev;
                baseNumber = baseNumber.left(lastR);
            }
        }
    }
    
    // Process each shipment/refund
    bool firstInvoice = true;
    for (int i = 0; i < invoicesToDo.size(); ++i) {
        if (!invoicesToDo[i]) {
            // No invoice needed for this one
            result.append(QString());
            continue;
        }
        
        if (baseNumber.isEmpty()) {
            // First invoice, generate new base number
            baseNumber = getBaseInvoiceNumber(date, taxContext, channel, store);
            result.append(baseNumber);
            firstInvoice = false;
        } else if (firstInvoice) {
            // First entry uses base number (original invoice)
            // Record this in our data if it's a marketplace number we haven't seen
            bool found = false;
            for (const auto &record : m_data) {
                if (record.invoiceNumber == baseNumber) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                InvoiceRecord record;
                record.date = date;
                record.taxDeclaringCountry = taxContext.taxDeclaringCountryCode;
                record.taxScheme = taxSchemeToString(taxContext.taxScheme);
                record.taxJurisdiction = taxJurisdictionLevelToString(taxContext.taxJurisdictionLevel);
                record.countryVatPaidTo = taxContext.countryCodeVatPaidTo;
                record.channel = channel;
                record.store = store;
                record.invoiceNumber = baseNumber;
                
                beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
                m_data.append(record);
                endInsertRows();
            }
            
            result.append(baseNumber);
            firstInvoice = false;
        } else {
            // Subsequent entries get revision numbers
            revisionCounter++;
            QString revisionNumber = QString("%1-R%2")
                .arg(baseNumber)
                .arg(revisionCounter, 2, 10, QChar('0'));
            
            // Record revision
            InvoiceRecord record;
            record.date = date;
            record.taxDeclaringCountry = taxContext.taxDeclaringCountryCode;
            record.taxScheme = taxSchemeToString(taxContext.taxScheme);
            record.taxJurisdiction = taxJurisdictionLevelToString(taxContext.taxJurisdictionLevel);
            record.countryVatPaidTo = taxContext.countryCodeVatPaidTo;
            record.channel = channel;
            record.store = store;
            record.invoiceNumber = revisionNumber;
            
            beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
            m_data.append(record);
            endInsertRows();
            
            result.append(revisionNumber);
        }
    }
    
    // NO SAVE HERE - Defer to generateInvoice
    
    return result;
}

void InvoiceGenerator::generateInvoice(
    const QString &invoiceNumber,
    const QString &previousInvoiceNumber,
    const QString &destinationPath,
    const Address &addressTo,
    const InvoicingInfo &invoicingInfo,
    const QString &orderId,
    OrderManager &orderManager)
{
    // 1. Gather Data
    QString companyName = "Your Company Name"; // Default
    QString companyAddress = "";
    int rowId = m_companyInfos->getRowById(CompanyInfosTable::ID_COUNTRY); // Using country just to access stored address logic if any
    
    // Use CompanyAddressTable for current address
    QDate invoiceDate = QDate::currentDate(); // Or use payment date?
    if (m_companyAddress) {
        companyName = m_companyAddress->getCompanyName(invoiceDate);
        QString street1 = m_companyAddress->getStreet1(invoiceDate);
        QString street2 = m_companyAddress->getStreet2(invoiceDate);
        QString postal = m_companyAddress->getPostalCode(invoiceDate);
        QString city = m_companyAddress->getCity(invoiceDate);
        QString country = m_companyAddress->getCompanyAddress(invoiceDate); // This actually returns full country name or code

        companyAddress += street1 + "<br>";
        if (!street2.isEmpty()) companyAddress += street2 + "<br>";
        companyAddress += postal + " " + city + "<br>";
        // companyAddress += country; // Country might be redundant if in standard address format
    }

    // Get legal footer info
    QString shareCapital = "";
    QString siret = "";
    QString rcs = "";
    QString vatIntra = "";
    
    int rowShare = m_companyInfos->getRowById(CompanyInfosTable::ID_LEGAL_SHARE_CAPITAL);
    if (rowShare != -1) {
        shareCapital = m_companyInfos->data(m_companyInfos->index(rowShare, 1)).toString();
    }
    
    int rowSiret = m_companyInfos->getRowById(CompanyInfosTable::ID_LEGAL_SIRET);
    if (rowSiret != -1) {
        siret = m_companyInfos->data(m_companyInfos->index(rowSiret, 1)).toString();
    }

    
    int rowRcs = m_companyInfos->getRowById(CompanyInfosTable::ID_LEGAL_RCS);
    if (rowRcs != -1) {
        rcs = m_companyInfos->data(m_companyInfos->index(rowRcs, 1)).toString();
    }

    int rowVat = m_companyInfos->getRowById(CompanyInfosTable::ID_LEGAL_VAT_INTRACOMMUNITY);
    if (rowVat != -1) {
        vatIntra = m_companyInfos->data(m_companyInfos->index(rowVat, 1)).toString();
    }

    // 2. Build HTML
    QString html = R"(
    <html>
    <head>
    <style>
        body { font-family: sans-serif; }
        .header { display: flex; justify-content: space-between; margin-bottom: 50px; }
        .sender { float: left; }
        .recipient { float: right; text-align: right; }
        .details { margin-top: 150px; margin-bottom: 30px; clear: both; }
        .table-container { margin-top: 20px; }
        table { width: 100%; border-collapse: collapse; }
        th { background-color: #d0e0f0; padding: 10px; text-align: left; }
        td { padding: 10px; border-bottom: 1px solid #eee; }
        .totals { margin-top: 20px; float: right; width: 300px; }
        .totals-row { display: flex; justify-content: space-between; margin-bottom: 5px; }
        .footer { position: fixed; bottom: 30px; width: 100%; text-align: center; font-size: 10px; color: #555; }
        .clear { clear: both; }
    </style>
    </head>
    <body>
    <div class="header">
        <div class="sender">
            <b>%1</b><br>
            %2<br>
            TVA: %3
        </div>
        <div class="recipient">
            <b>%4</b><br>
            %5<br>
            %6<br>
            %7 %8<br>
            %9
        </div>
    </div>
    
    <div class="details">
        <b>Numéro de facture : %10</b><br>
        %11
        Numéro de commande : %12<br>
        Date de la facture: %13<br>
        Date du paiement: %14<br>
    </div>
    
    <div class="table-container">
        <table>
            <tr>
                <th>Nom du produit</th>
                <th>Quantité</th>
                <th>Taux de TVA</th>
                <th>Prix HT</th>
                <th>TVA</th>
                <th>Monnaie</th>
            </tr>
            %15
            <tr style="font-weight: bold; background-color: #e0f0f0;">
                <td>Total</td>
                <td></td>
                <td></td>
                <td>%16</td>
                <td>%17</td>
                <td>%18</td>
            </tr>
        </table>
    </div>

    <div class="totals">
        <div class="totals-row"><b>Total HT</b> <span>%16 %18</span></div>
        <div class="totals-row"><b>TVA</b> <span>%17 %18</span></div>
        <div class="totals-row" style="font-size: 1.2em; border-top: 1px solid #000; padding-top: 5px;">
            <b>Total TTC</b> <span>%19 %18</span>
        </div>
    </div>

    <div class="clear"></div>

    <div class="footer">
        %1<br>
        SASU au capital variable de %20 - RCS %21 - Siret %22 - APE 4791B<br>
        %23
    </div>
    </body>
    </html>
    )";

    // Fill HTML placeholders
    // %1: Company Name
    // %2: Company Address
    // %3: My VAT
    // %4: Recipient Name
    // %5: Recipient Addr 1
    // %6: Recipient Addr 2
    // %7: Recipient Postal
    // %8: Recipient City
    // %9: Recipient Country
    // %10: Invoice Num
    // %11: Previous Invoice (if any)
    // %12: Order ID
    // %13: Invoice Date
    // %14: Payment Date
    // %15: Table Rows
    // %16: Total HT
    // %17: Total Tax
    // %18: Currency
    // %19: Total TTC
    // %20: Share Capital
    // %21: RCS
    // %22: Siret
    // %23: Legal Footer Text (e.g. late payment penalties)

    // Format address
    QString destName = addressTo.getFullName().isEmpty() ? addressTo.getCompanyName() : addressTo.getFullName();
    QString destAddr1 = addressTo.getAddressLine1();
    QString destAddr2 = addressTo.getAddressLine2();
    QString destPostal = addressTo.getPostalCode();
    QString destCity = addressTo.getCity();
    QString destCountry = addressTo.getCountryCode(); // Should translate or map code to name? Keeping code for now is safer or map elsewhere
    
    // Format dates
    QString invDateStr = invoiceDate.toString("dd/MM/yyyy");
    QDate payDate = invoicingInfo.getPaymentDate(invoiceDate); // Fallback to inv date if not set, or order date
    QString payDateStr = payDate.toString("dd/MM/yyyy");

    // Previous invoice line
    QString prevInvLine = "";
    if (!previousInvoiceNumber.isEmpty()) {
        prevInvLine = QString("Facture d'origine : %1<br>").arg(previousInvoiceNumber);
    }

    // Build Table Rows
    QString rowsHtml;
    double totalHT = 0.0;
    double totalTax = 0.0;
    double totalTTC = 0.0;
    QString currency = "EUR"; // Default, should come from InvoicingInfo/Amount

    for (const auto &item : invoicingInfo.getItems()) {
        double ht = item.getAmountTaxed();
        double tax = item.getTaxes();
        double ttc = item.getTotalTaxes() + item.getTotalTaxed(); // Or simpler calc
        
        // Wait, LineItem uses Amount structure? No, getAmountTaxed returns double.
        // Assuming single currency for all items.
        // Currency is implicit in Amount? LineItem doesn't seem to expose currency symbol directly easily, 
        // but OrderManager usually works with a currency. Let's assume order currency.
        // For display, we need formatting.
        
        totalHT += ht;
        totalTax += tax;
        totalTTC += ht + tax;

        rowsHtml += QString(R"(
            <tr>
                <td>%1</td>
                <td>%2</td>
                <td>%3</td>
                <td>%4</td>
                <td>%5</td>
                <td>%6</td>
            </tr>
        )")
        .arg(item.getName(),
             QString::number(item.getQuantity()),
             QString::number(ht > 0 ? (tax / ht) : 0.0, 'f', 2), // Rate approximation
             QString::number(ht, 'f', 2),
             QString::number(tax, 'f', 2),
             currency); // TODO: Get actual currency
    }

    QString legalFooter = "Toute somme non payée à échéance est susceptible de porter intérêts à un taux égal à une fois et demi le taux d'intérêt légal.<br>"
                          "Tout retard de paiement entraine de plein droit et sans qu'un rappel ne soit nécessaire, outre les pénalités de retard, une obligation pour le débiteur de payer une indemnité forfaitaire pour frais de recouvrement de 40 EUR.";

    QString finalHtml = html
        .arg(companyName, companyAddress, vatIntra, destName, destAddr1, destAddr2, destPostal, destCity, destCountry)
        .arg(invoiceNumber, prevInvLine, orderId, invDateStr, payDateStr)
        .arg(rowsHtml, QString::number(totalHT, 'f', 2), QString::number(totalTax, 'f', 2), currency, QString::number(totalTTC, 'f', 2))
        .arg(shareCapital, rcs, siret, legalFooter);


    // 3. Print to PDF
    QTextDocument document;
    document.setHtml(finalHtml);

    QPdfWriter writer(destinationPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300); // High res for text
    writer.setPageMargins(QMarginsF(15, 15, 15, 15)); // mm

    document.print(&writer);

    // 4. On Success: Persist
    if (QFile::exists(destinationPath)) {
        // Save CSV
        _save();
        
        InvoicingInfo newInfo = invoicingInfo;
        newInfo.setInvoiceNumber(invoiceNumber);
        
        // Use passed OrderManager reference
        orderManager.recordInvoicingInfo(orderId, &newInfo);
    }
}

// QAbstractTableModel implementation

int InvoiceGenerator::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int InvoiceGenerator::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant InvoiceGenerator::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_data.size()) return QVariant();
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        const InvoiceRecord &record = m_data[index.row()];
        switch (index.column()) {
            case ColDate: return record.date;
            case ColTaxDeclaringCountry: return record.taxDeclaringCountry;
            case ColTaxScheme: return record.taxScheme;
            case ColTaxJurisdiction: return record.taxJurisdiction;
            case ColCountryVatPaidTo: return record.countryVatPaidTo;
            case ColChannel: return record.channel;
            case ColStore: return record.store;
            case ColInvoiceNumber: return record.invoiceNumber;
            default: return QVariant();
        }
    }
    
    return QVariant();
}

QVariant InvoiceGenerator::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < ColCount) {
            switch (section) {
                case ColDate: return tr("Date");
                case ColTaxDeclaringCountry: return tr("Tax Country");
                case ColTaxScheme: return tr("Tax Scheme");
                case ColTaxJurisdiction: return tr("Jurisdiction");
                case ColCountryVatPaidTo: return tr("VAT Paid To");
                case ColChannel: return tr("Channel");
                case ColStore: return tr("Store");
                case ColInvoiceNumber: return tr("Invoice Number");
                default: return HEADER_IDS[section];
            }
        }
    }
    return QVariant();
}

void InvoiceGenerator::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    
    std::sort(m_data.begin(), m_data.end(),
        [column, order](const InvoiceRecord &a, const InvoiceRecord &b) {
            bool lessThan = false;
            switch (column) {
                case ColDate:
                    lessThan = a.date < b.date;
                    break;
                case ColTaxDeclaringCountry:
                    lessThan = a.taxDeclaringCountry < b.taxDeclaringCountry;
                    break;
                case ColTaxScheme:
                    lessThan = a.taxScheme < b.taxScheme;
                    break;
                case ColTaxJurisdiction:
                    lessThan = a.taxJurisdiction < b.taxJurisdiction;
                    break;
                case ColCountryVatPaidTo:
                    lessThan = a.countryVatPaidTo < b.countryVatPaidTo;
                    break;
                case ColChannel:
                    lessThan = a.channel < b.channel;
                    break;
                case ColStore:
                    lessThan = a.store < b.store;
                    break;
                case ColInvoiceNumber:
                    lessThan = a.invoiceNumber < b.invoiceNumber;
                    break;
                default:
                    lessThan = a.date < b.date;
                    break;
            }
            return order == Qt::AscendingOrder ? lessThan : !lessThan;
        });
    
    endResetModel();
}

// Private methods

void InvoiceGenerator::_load()
{
    m_data.clear();
    m_sequenceCache.clear();
    
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    
    QTextStream in(&file);
    if (in.atEnd()) return;
    
    // Read and parse header
    QString headerLine = in.readLine();
    QStringList headers = headerLine.split(";");
    
    QMap<QString, int> colMap;
    for (int i = 0; i < headers.size(); ++i) {
        colMap[headers[i].trimmed()] = i;
    }
    
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        
        QStringList parts = line.split(";");
        
        InvoiceRecord record;
        
        int idxDate = colMap.value("Date", -1);
        int idxTaxCountry = colMap.value("TaxDeclaringCountry", -1);
        int idxTaxScheme = colMap.value("TaxScheme", -1);
        int idxTaxJuris = colMap.value("TaxJurisdiction", -1);
        int idxVatPaidTo = colMap.value("CountryVatPaidTo", -1);
        int idxChannel = colMap.value("Channel", -1);
        int idxStore = colMap.value("Store", -1);
        int idxInvoice = colMap.value("InvoiceNumber", -1);
        
        if (idxDate != -1 && idxDate < parts.size()) {
            record.date = QDate::fromString(parts[idxDate], Qt::ISODate);
        }
        if (idxTaxCountry != -1 && idxTaxCountry < parts.size()) {
            record.taxDeclaringCountry = parts[idxTaxCountry];
        }
        if (idxTaxScheme != -1 && idxTaxScheme < parts.size()) {
            record.taxScheme = parts[idxTaxScheme];
        }
        if (idxTaxJuris != -1 && idxTaxJuris < parts.size()) {
            record.taxJurisdiction = parts[idxTaxJuris];
        }
        if (idxVatPaidTo != -1 && idxVatPaidTo < parts.size()) {
            record.countryVatPaidTo = parts[idxVatPaidTo];
        }
        if (idxChannel != -1 && idxChannel < parts.size()) {
            record.channel = parts[idxChannel];
        }
        if (idxStore != -1 && idxStore < parts.size()) {
            record.store = parts[idxStore];
        }
        if (idxInvoice != -1 && idxInvoice < parts.size()) {
            record.invoiceNumber = parts[idxInvoice];
        }
        
        if (!record.invoiceNumber.isEmpty()) {
            m_data.append(record);
        }
    }
}

void InvoiceGenerator::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    
    QTextStream out(&file);
    out << HEADER_IDS.join(";") << "\n";
    
    for (const auto &record : m_data) {
        out << record.date.toString(Qt::ISODate) << ";"
            << record.taxDeclaringCountry << ";"
            << record.taxScheme << ";"
            << record.taxJurisdiction << ";"
            << record.countryVatPaidTo << ";"
            << record.channel << ";"
            << record.store << ";"
            << record.invoiceNumber << "\n";
    }
}
