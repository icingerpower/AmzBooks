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
#include "VatNumbersTable.h"
#include "CountriesEu.h"
#include "orders/InvoicingInfo.h"
#include "orders/Address.h"
#include "ExceptionWithTitleText.h"

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
    {"Sale service", "SAL"},
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
    "InvoiceNumber",
    "ShipmentId",
    "ActivityId"
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
    const VatNumbersTable *vatNumbers,
    QObject *parent)
    : QAbstractTableModel(parent)
    , m_companyInfos(companyInfos)
    , m_companyAddress(companyAddress)
    , m_currencyRates(currencyRates)
    , m_vatNumbers(vatNumbers)
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
    for (const auto &record : std::as_const(m_data)) {
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
    const QString &store,
    const QString &shipmentId)
{
    // Idempotent: look up m_data for an existing record bound to this shipmentId.
    // Using m_data (rather than an in-memory cache) ensures the mapping survives
    // across InvoiceGenerator instances and is consistent with removeInvoiceRecord.
    if (!shipmentId.isEmpty()) {
        for (const auto &record : m_data) {
            if (record.shipmentId == shipmentId) {
                return record.invoiceNumber;
            }
        }
    }

    QString contextKey = _buildContextKey(date, taxContext, channel, store);
    int sequence = _getNextSequenceForContext(contextKey);

    QString invoiceNumber = QString("%1-%2")
        .arg(contextKey)
        .arg(sequence, 3, 10, QChar('0'));

    // Record this invoice (with the shipmentId for future idempotent lookups)
    InvoiceRecord record;
    record.date = date;
    record.taxDeclaringCountry = taxContext.taxDeclaringCountryCode;
    record.taxScheme = taxSchemeToString(taxContext.taxScheme);
    record.taxJurisdiction = taxJurisdictionLevelToString(taxContext.taxJurisdictionLevel);
    record.countryVatPaidTo = taxContext.countryCodeVatPaidTo;
    record.channel = channel;
    record.store = store;
    record.invoiceNumber = invoiceNumber;
    record.shipmentId = shipmentId;

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
    const std::optional<QString> &existingInvoiceNumber,
    const QStringList &shipmentIds,
    const OrderManager *orderManager,
    const QStringList &activityIds)
{
    QStringList result;

    if (invoicesToDo.isEmpty()) {
        return result;
    }

    // Per-shipment state: the base invoice number, revision counter, and whether the
    // base number has already been appended to the result.
    struct ShipmentState {
        QString baseNumber;
        int revCounter = 0;
        bool baseAppended = false;
    };
    QHash<QString, ShipmentState> shipmentStates;

    // If an existing invoice number is provided, pre-map it to the first invoiceable
    // shipment so that subsequent entries with the same shipmentId become revisions.
    if (existingInvoiceNumber.has_value() && !existingInvoiceNumber->isEmpty()) {
        for (int i = 0; i < invoicesToDo.size(); ++i) {
            if (!invoicesToDo[i]) continue;

            const QString sid = shipmentIds.value(i);
            ShipmentState &state = shipmentStates[sid];
            state.baseNumber = existingInvoiceNumber.value();

            // Strip any revision suffix already present in the existing number
            int lastR = state.baseNumber.lastIndexOf("-R");
            if (lastR != -1) {
                QString revPart = state.baseNumber.mid(lastR + 2);
                bool ok;
                int existingRev = revPart.toInt(&ok);
                if (ok) {
                    state.revCounter = existingRev;
                    state.baseNumber = state.baseNumber.left(lastR);
                }
            }

            // Record the existing number in our data if it is not yet tracked
            bool found = false;
            for (const auto &record : m_data) {
                if (record.invoiceNumber == existingInvoiceNumber.value()) {
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
                record.invoiceNumber = existingInvoiceNumber.value();
                record.shipmentId = sid;

                beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
                m_data.append(record);
                endInsertRows();
            }

            break; // Only pre-populate the first invoiceable entry
        }
    }

    // If an OrderManager is provided, pre-populate states for shipmentIds whose base
    // invoice exists in OrderManager.  This covers:
    //   (a) sales invoiced externally (e.g. Amazon FBA) — stored in OrderManager only;
    //   (b) sales previously generated by InvoiceGenerator — already in m_data but
    //       also recorded in OrderManager.
    // In both cases, refunds should receive -R01, -R02, etc. relative to the sale
    // invoice rather than a new sequential base number.
    // We always prefer the OrderManager value over anything already placed in
    // shipmentStates by the m_data scan above, because OrderManager stores the
    // canonical invoice number (including the correct base, free of -Rxx corruption).
    if (orderManager) {
        for (const QString &sid : shipmentIds) {
            if (sid.isEmpty())
                continue;

            auto existingInfo = orderManager->getInvoicingInfo(sid);
            if (existingInfo && existingInfo->getInvoiceNumber().has_value()
                    && !existingInfo->getInvoiceNumber()->isEmpty()) {
                QString baseNumber = *existingInfo->getInvoiceNumber();
                int revCounter = 0;
                // Strip any trailing -Rxx suffix to recover the true base number.
                // This guards against stale/corrupt data where a refund invoice number
                // was accidentally stored as the "sale" invoice, which would otherwise
                // produce double-R suffixes (e.g. ...-001-R01-R01).
                int lastR = baseNumber.lastIndexOf(QStringLiteral("-R"));
                if (lastR != -1) {
                    QString revPart = baseNumber.mid(lastR + 2);
                    bool ok = false;
                    int existingRev = revPart.toInt(&ok);
                    if (ok) {
                        revCounter = existingRev;
                        baseNumber = baseNumber.left(lastR);
                    }
                }
                // Pre-mark the state as "base already appended" so that every
                // occurrence of this shipmentId generates a -Rxx revision.
                ShipmentState state;
                state.baseNumber   = baseNumber;
                state.revCounter   = revCounter;
                state.baseAppended = true;
                shipmentStates[sid] = state;
            }
        }
    }

    // Process each entry
    for (int i = 0; i < invoicesToDo.size(); ++i) {
        if (!invoicesToDo[i]) {
            result.append(QString());
            continue;
        }

        const QString sid = shipmentIds.value(i);

        if (!shipmentStates.contains(sid)) {
            // New shipment: generate a fresh sequential base number
            ShipmentState state;
            state.baseNumber = getBaseInvoiceNumber(date, taxContext, channel, store, sid);
            state.baseAppended = true;
            shipmentStates[sid] = state;
            result.append(state.baseNumber);
        } else {
            ShipmentState &state = shipmentStates[sid];
            if (!state.baseAppended) {
                // First occurrence of a pre-populated existing number
                state.baseAppended = true;
                result.append(state.baseNumber);
            } else {
                // Subsequent entry for the same shipment: generate a revision
                state.revCounter++;
                QString revisionNumber = QString("%1-R%2")
                    .arg(state.baseNumber)
                    .arg(state.revCounter, 2, 10, QChar('0'));

                InvoiceRecord record;
                record.date = date;
                record.taxDeclaringCountry = taxContext.taxDeclaringCountryCode;
                record.taxScheme = taxSchemeToString(taxContext.taxScheme);
                record.taxJurisdiction = taxJurisdictionLevelToString(taxContext.taxJurisdictionLevel);
                record.countryVatPaidTo = taxContext.countryCodeVatPaidTo;
                record.channel = channel;
                record.store = store;
                record.invoiceNumber = revisionNumber;
                record.shipmentId = sid;
                record.activityId = activityIds.value(i);

                beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
                m_data.append(record);
                endInsertRows();

                result.append(revisionNumber);
            }
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
    OrderManager &orderManager,
    const QDate &invoiceDate,
    const QString &shipmentId)
{
    // 1. Gather Data
    QString companyName = "Your Company Name"; // Default
    QString companyAddress = "";

    if (!invoiceDate.isValid())
        throw ExceptionWithTitleText(tr("Invalid Invoice Date"),
                                     tr("The invoice date is invalid. This is a bug — please report it."));
    QDate actualInvoiceDate = invoiceDate;
    if (m_companyAddress) {
        companyName = m_companyAddress->getCompanyName(actualInvoiceDate);
        QString street1 = m_companyAddress->getStreet1(actualInvoiceDate);
        QString street2 = m_companyAddress->getStreet2(actualInvoiceDate);
        QString postal = m_companyAddress->getPostalCode(actualInvoiceDate);
        QString city = m_companyAddress->getCity(actualInvoiceDate);

        companyAddress += street1 + "<br>";
        if (!street2.isEmpty()) companyAddress += street2 + "<br>";
        companyAddress += postal + " " + city + "<br>";
    }

    // Company VAT number (from VatNumbersTable, keyed by company country)
    QString companyCountryCode = m_companyInfos->getCompanyCountryCode();
    QString companyVatNumber;
    if (m_vatNumbers) {
        companyVatNumber = m_vatNumbers->getVatNumber(companyCountryCode);
    }

    // Get legal footer info
    QString shareCapital = "";
    int rowShare = m_companyInfos->getRowById(CompanyInfosTable::ID_LEGAL_SHARE_CAPITAL);
    if (rowShare != -1) {
        shareCapital = m_companyInfos->data(m_companyInfos->index(rowShare, 1)).toString();
    }
    QString siret    = m_companyInfos->getLegalID();
    QString rcs      = m_companyInfos->getLegalRCS();
    QString vatIntra = m_companyInfos->getLegalVatIntracommunity();

    // 2. Build HTML
    QString html = R"(
    <html>
    <head>
    <style>
        body { font-family: sans-serif; }
        .header { display: flex; justify-content: space-between; margin-bottom: 15px; }
        .sender { float: left; }
        .recipient { float: right; text-align: right; }
        .details { margin-top: 20px; margin-bottom: 10px; clear: both; }
        .table-container { margin-top: 10px; }
        table { width: 100%; border-collapse: collapse; }
        th { background-color: #d0e0f0; padding: 6px; text-align: left; }
        td { padding: 6px; border-bottom: 1px solid #eee; }
        .totals { margin-top: 10px; float: right; width: 300px; }
        .totals-row { display: flex; justify-content: space-between; margin-bottom: 5px; }
        .footer { position: fixed; bottom: 30px; width: 100%; text-align: center; font-size: 16px; color: #555; }
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
    
    %25
    %24

    <div class="table-container">
        <table>
            <tr>
                <th>Nom du produit</th>
                <th>Quantité</th>
                <th>Taux de TVA</th>
                <th>Prix HT</th>
                <th>TVA</th>
                <th>Prix TTC</th>
                <th>Monnaie</th>
            </tr>
            %15
            <tr style="font-weight: bold; background-color: #e0f0f0;">
                <td>Total</td>
                <td></td>
                <td></td>
                <td>%16</td>
                <td>%17</td>
                <td>%19</td>
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
        %26
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
    QString invDateStr = actualInvoiceDate.toString("dd/MM/yyyy");
    QDate payDate = invoicingInfo.getPaymentDate(actualInvoiceDate); // Fallback to inv date if not set, or order date
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
    const QString &companyCurrency = m_companyInfos->getCurrency();
    const QString &infoCurrency = invoicingInfo.getCurrency();
    const QString currency = infoCurrency.isEmpty() ? companyCurrency : infoCurrency;
    const bool showConverted = (currency != companyCurrency) && (m_currencyRates != nullptr);

    for (const auto &item : invoicingInfo.getItems()) {
        const double ht = item.getAmountUntaxed();
        const double tax = item.getTaxes();
        const double quantity = item.getQuantity();
        const double vatRate = (ht != 0.0) ? (tax / ht * 100.0) : 0.0;
        const double ttc = ht + tax;

        totalHT  += quantity * ht;
        totalTax += quantity * tax;
        totalTTC += quantity * ttc;

        rowsHtml += QString(R"(
            <tr>
                <td>%1</td>
                <td>%2</td>
                <td>%3 %</td>
                <td>%4</td>
                <td>%5</td>
                <td>%6</td>
                <td>%7</td>
            </tr>
        )")
        .arg(item.getName(),
             QString::number(quantity, 'f', 1),
             QString::number(vatRate, 'f', 2),
             QString::number(ht, 'f', 2),
             QString::number(tax, 'f', 2),
             QString::number(ttc, 'f', 2),
             currency);
    }

    // Converted totals block — only shown when order currency differs from company currency.
    QString convertedTotalsHtml;
    if (showConverted) {
        try {
            const double convHT  = m_currencyRates->convert(totalHT,  currency, companyCurrency, invoiceDate);
            const double convTax = m_currencyRates->convert(totalTax, currency, companyCurrency, invoiceDate);
            const double convTTC = m_currencyRates->convert(totalTTC, currency, companyCurrency, invoiceDate);
            convertedTotalsHtml =
                QString("<div class=\"totals-row\" style=\"color:#555;border-top:1px dashed #aaa;margin-top:6px;padding-top:5px;\">"
                        "<b>= Total HT</b> <span>%1 %4</span></div>"
                        "<div class=\"totals-row\" style=\"color:#555;\">"
                        "<b>= TVA</b> <span>%2 %4</span></div>"
                        "<div class=\"totals-row\" style=\"color:#555;font-weight:bold;\">"
                        "<b>= Total TTC</b> <span>%3 %4</span></div>")
                .arg(QString::number(convHT,  'f', 2),
                     QString::number(convTax, 'f', 2),
                     QString::number(convTTC, 'f', 2),
                     companyCurrency);
        } catch (...) {
            // Conversion rate unavailable — omit rather than show wrong data
        }
    }

    // EU-to-EU without VAT: both company and destination are EU members and no VAT was charged
    // (reverse charge / autoliquidation). In this case, append the intracommunity VAT mention.
    const QString &destCountryCode = addressTo.getCountryCode();
    bool isEuToEuNoVat = CountriesEu::all().contains(companyCountryCode)
                      && CountriesEu::all().contains(destCountryCode)
                      && qFuzzyIsNull(totalTax);

    QString legalFooter = m_companyInfos->getInvoiceLegalBottom();
    if (isEuToEuNoVat && !vatIntra.isEmpty()) {
        legalFooter = vatIntra + "<br>" + legalFooter;
    }

    QString vatOnPaymentHtml;
    if (invoicingInfo.getVatOnPayment()) {
        vatOnPaymentHtml = QString("<p><i>%1</i></p>").arg(m_companyInfos->getVatOnPaymentText());
    }

    // Human-readable tax context line — look up the invoice record for channel + scheme
    QString taxContextHtml;
    {
        static const QHash<QString, QString> SCHEME_LABELS = {
            {"DomesticVat",                "Régime Normal"},
            {"EuOssUnion",                 "OSS Union"},
            {"EuOssNonUnion",              "OSS Non-Union"},
            {"EuIoss",                     "IOSS"},
            {"ImportVat",                  "TVA à l'import"},
            {"ReverseChargeImport",        "Autoliquidation Import"},
            {"ReverseChargeDomestic",      "Autoliquidation"},
            {"MarketplaceDeemedSupplier",  "Marketplace Fournisseur Présumé"},
            {"Exempt",                     "Exonéré"},
            {"OutOfScope",                 "Hors Champ"},
            {"Unknown",                    "Inconnu"},
        };

        QString channel;
        QString taxSchemeStr;
        for (const auto &record : m_data) {
            if (record.invoiceNumber == invoiceNumber) {
                channel      = record.channel;
                taxSchemeStr = record.taxScheme;
                break;
            }
        }
        if (channel == "Sale service") channel = "Service";
        const QString regimeLabel = SCHEME_LABELS.value(taxSchemeStr, taxSchemeStr);
        taxContextHtml = QString("<p>%1 %2 =&gt; %3 (%4)</p>")
            .arg(channel, companyCountryCode, destCountryCode, regimeLabel);
    }

    QString finalHtml = html
        .arg(companyName, companyAddress, companyVatNumber, destName, destAddr1, destAddr2, destPostal, destCity, destCountry)
        .arg(invoiceNumber, prevInvLine, orderId, invDateStr, payDateStr)
        .arg(rowsHtml, QString::number(totalHT, 'f', 2), QString::number(totalTax, 'f', 2), currency, QString::number(totalTTC, 'f', 2))
        .arg(shareCapital, rcs, siret, legalFooter)
        .arg(vatOnPaymentHtml)
        .arg(taxContextHtml)
        .arg(convertedTotalsHtml);  // %26


    // 3. Print to PDF
    QTextDocument document;
    document.setHtml(finalHtml);

    QPdfWriter writer(destinationPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300); // High res for text
    writer.setPageMargins(QMarginsF(3.0, 3.0, 3.0, 3.0)); // mm

    document.print(&writer);

    // 4. On Success: Persist
    if (QFile::exists(destinationPath)) {
        // Save CSV
        _save();
        
        InvoicingInfo newInfo = invoicingInfo;
        newInfo.setInvoiceNumber(invoiceNumber);

        // Record under the shipment root ID when available (preferred), otherwise
        // fall back to orderId for backward compatibility.  Recording under the
        // shipment root ID ensures that subsequent Phase-2 lookups in
        // get_channel_site_ShipmentAndRefundsNoInvoices find the invoice via the
        // JOIN on COALESCE(root_id, id) = shipment_root_id.
        const QString recordId = shipmentId.isEmpty() ? orderId : shipmentId;
        orderManager.recordInvoicingInfo(recordId, &newInfo);
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
        int idxShipmentId = colMap.value("ShipmentId", -1);
        int idxActivityId = colMap.value("ActivityId", -1);

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
        if (idxShipmentId != -1 && idxShipmentId < parts.size()) {
            record.shipmentId = parts[idxShipmentId];
        }
        if (idxActivityId != -1 && idxActivityId < parts.size()) {
            record.activityId = parts[idxActivityId];
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
            << record.invoiceNumber << ";"
            << record.shipmentId << ";"
            << record.activityId << "\n";
    }
}

void InvoiceGenerator::removeInvoiceByNumber(const QString &invoiceNumber)
{
    if (invoiceNumber.isEmpty())
        return;

    // Match the base record and any revision records (e.g. "INV-001-R01", "INV-001-R02")
    const QString revisionPrefix = invoiceNumber + "-R";
    int removed = 0;
    for (int i = m_data.size() - 1; i >= 0; --i) {
        const QString &num = m_data[i].invoiceNumber;
        if (num == invoiceNumber || num.startsWith(revisionPrefix)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_data.removeAt(i);
            endRemoveRows();
            ++removed;
        }
    }

    if (removed > 0) {
        m_sequenceCache.clear();
        _save();
    }
}

void InvoiceGenerator::regenerateInvoices(
    const QDir &folderTo,
    const QDate &dateFrom,
    const QDate &dateTo,
    OrderManager &orderManager)
{
    // Iterate a snapshot of the records so that any implicit saves inside
    // generateInvoice do not invalidate our iteration.
    const QList<InvoiceRecord> snapshot = m_data;

    const Address emptyAddr("", "", "", "", "", "", "", "", "", "", "", "");

    for (const InvoiceRecord &record : snapshot) {
        if (record.date < dateFrom || record.date > dateTo)
            continue;
        if (record.shipmentId.isEmpty())
            continue;

        // Retrieve InvoicingInfo and address from the OrderManager.
        // For revision (refund) records, activityId points to the refund entry
        // (e.g. "3105_refund") while shipmentId holds the eventId ("3105").
        // Using activityId ensures we fetch the refund's invoicingInfo (-124.99)
        // rather than the original order's invoicingInfo (+124.99).
        const QString lookupId = record.activityId.isEmpty() ? record.shipmentId : record.activityId;
        QSharedPointer<InvoicingInfo> info = orderManager.getInvoicingInfo(lookupId);
        if (!info) {
            continue;
        }

        // Address is always keyed by the order/eventId, same for order and refund.
        QSharedPointer<Address> addrPtr = orderManager.getAddressTo(record.shipmentId);
        const Address &addr = addrPtr ? *addrPtr : emptyAddr;

        // Determine previous invoice number for revision invoices (e.g. BASE-R01 → BASE)
        QString prevNumber;
        const int rIdx = record.invoiceNumber.lastIndexOf("-R");
        if (rIdx != -1) {
            const QString suffix = record.invoiceNumber.mid(rIdx + 2);
            bool ok;
            suffix.toInt(&ok);
            if (ok)
                prevNumber = record.invoiceNumber.left(rIdx);
        }

        // Build output path mirroring the layout used by generateInvoices()
        QString sanitized = record.invoiceNumber;
        sanitized.replace('/', '-').replace('\\', '-');
        QString subDirName = QString("%1/%2").arg(record.date.year()).arg(record.date.month(), 2, 10, QChar('0'));
        QDir subDir(folderTo.filePath(subDirName));
        subDir.mkpath(".");
        const QString pdfPath = subDir.absoluteFilePath(sanitized + ".pdf");

        // Pass activityId as the shipmentId param so generateInvoice records
        // the invoicingInfo under the correct key (e.g. "3105_refund" for refunds)
        // rather than overwriting the base order's entry under "3105".
        generateInvoice(record.invoiceNumber, prevNumber, pdfPath,
                        addr, *info, record.shipmentId, orderManager,
                        record.date, record.activityId);
    }
}

void InvoiceGenerator::removeInvoiceRecord(const QString &shipmentId)
{
    if (shipmentId.isEmpty())
        return;

    int removed = 0;
    for (int i = m_data.size() - 1; i >= 0; --i) {
        if (m_data[i].shipmentId == shipmentId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_data.removeAt(i);
            endRemoveRows();
            ++removed;
        }
    }

    if (removed > 0) {
        // Invalidate the sequence cache so sequences are recalculated from the updated m_data
        m_sequenceCache.clear();
        _save();
    }
}
