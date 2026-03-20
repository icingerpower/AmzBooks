#include "ImporterFileTemuVatEu.h"
#include "books/Activity.h"
#include "books/TaxResolver.h"
#include "orders/Shipment.h"
#include "orders/Refund.h"
#include "orders/InvoicingInfo.h"
#include "utils/CsvReader.h"
#include <QFileInfo>
#include <QDebug>

DECLARE_IMPORTER_FILE(ImporterFileTemuVatEu)

// Column name for departure country.
// The raw CSV has "PAYS DE DÉPART DE LA TRANSACTION " (quoted, trailing space), but
// CsvHeader stores trimmed keys, so the lookup must use the trimmed form.
static const QString COL_DEPART = "PAYS DE DÉPART DE LA TRANSACTION";

QString ImporterFileTemuVatEu::getLabel() const
{
    return QObject::tr("Temu EU VAT Report");
}

ActivitySource ImporterFileTemuVatEu::getActivitySource() const
{
    ActivitySource s;
    s.type = ActivitySourceType::Report;
    s.channel = CHANNEL_TEMU;
    s.reportOrMethode = QObject::tr("Temu EU VAT Report");
    return s;
}

QString ImporterFileTemuVatEu::getId() const
{
    return "TemuVatEu";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileTemuVatEu::getRequiredParams() const
{
    return {};
}

QString ImporterFileTemuVatEu::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

bool ImporterFileTemuVatEu::recomputeTaxes() const
{
    return false;
}

bool ImporterFileTemuVatEu::isWrongIfConflict() const
{
    return false;
}

bool ImporterFileTemuVatEu::fixRefundDate() const
{
    return true;
}

bool ImporterFileTemuVatEu::isGroupedOrders() const
{
    return true;
}

double ImporterFileTemuVatEu::parseEuropeanAmount(const QString &amountStr)
{
    QString s = amountStr.trimmed();
    if (s.isEmpty())
        return 0.0;
    // European format uses comma as decimal separator (e.g. "11,09" or "-2,72").
    s.replace(',', '.');
    bool ok = false;
    double v = s.toDouble(&ok);
    if (!ok)
    {
        qWarning() << "ImporterFileTemuVatEu: failed to parse amount:" << amountStr;
        return 0.0;
    }
    return v;
}

QDate ImporterFileTemuVatEu::parseTemuVatDate(const QString &dateStr)
{
    QString s = dateStr.trimmed();
    if (s.isEmpty())
        return QDate();

    // Try ISO format: "yyyy-MM-dd" (used in some report files, e.g. 2025-11-29)
    QDate date = QDate::fromString(s, "yyyy-MM-dd");
    if (date.isValid())
        return date;

    // Try French abbreviated format: "d déc. 2025" or "26 janv. 2026"
    static const QMap<QString, int> frMonths = {
        {"janv.", 1}, {"févr.", 2}, {"mars", 3},  {"avr.", 4},
        {"mai",   5}, {"juin",  6}, {"juil.", 7}, {"août", 8},
        {"sept.", 9}, {"oct.", 10}, {"nov.", 11}, {"déc.", 12}
    };

    QStringList parts = s.split(' ', Qt::SkipEmptyParts);
    if (parts.size() == 3)
    {
        bool dayOk = false;
        int day   = parts[0].toInt(&dayOk);
        int month = frMonths.value(parts[1], 0);
        bool yearOk = false;
        int year  = parts[2].toInt(&yearOk);
        if (dayOk && month > 0 && yearOk)
            return QDate(year, month, day);
    }

    qWarning() << "ImporterFileTemuVatEu: failed to parse date:" << dateStr;
    return QDate();
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileTemuVatEu::_loadReport(
    const QString &filePath,
    std::function<QCoro::Task<bool>(const QString &, const QString &)> callbackAddIfMissing)
{
    Q_UNUSED(callbackAddIfMissing)

    AbstractImporter::ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();

    CsvReader reader(filePath, ",", "\"", true, "\n", 0, "UTF-8");
    if (!reader.readAll())
    {
        result.errorReturned = "Failed to read CSV file: " + filePath;
        co_return result;
    }

    const auto *csvData = reader.dataRode();

    // --- Validate required columns -----------------------------------------------
    const QStringList requiredCols = {
        "N° DE COMMANDE",
        "DATE DE PAIEMENT DE LA COMMANDE",
        "TYPE DE VENTE",
        "SKU DU VENDEUR",
        COL_DEPART,                          // "PAYS DE DÉPART DE LA TRANSACTION " (trailing space)
        "PAYS D'ARRIVÉE DE LA TRANSACTION",
        "PRIX DES ARTICLES (HORS TVA)",
        "PRIX DE LA SUBVENTION (HORS TVA)",
        "PRIX D'EXPÉDITION (HORS TVA)",
        "TAXE TOTALE",
        "DEVISE",
        "ID DE FACTURE"
    };
    for (const QString &col : requiredCols)
    {
        if (!csvData->header.contains(col))
        {
            result.errorReturned = "Missing column: " + col;
            co_return result;
        }
    }

    // --- Column indices -----------------------------------------------------------
    int idxOrderId   = csvData->header.pos("N° DE COMMANDE");
    int idxDate      = csvData->header.pos("DATE DE PAIEMENT DE LA COMMANDE");
    int idxType      = csvData->header.pos("TYPE DE VENTE");
    int idxSku       = csvData->header.pos("SKU DU VENDEUR");
    int idxDepart    = csvData->header.pos(COL_DEPART);
    int idxArrival   = csvData->header.pos("PAYS D'ARRIVÉE DE LA TRANSACTION");
    int idxItemExcl  = csvData->header.pos("PRIX DES ARTICLES (HORS TVA)");
    int idxSubsidy   = csvData->header.pos("PRIX DE LA SUBVENTION (HORS TVA)");
    int idxShipping  = csvData->header.pos("PRIX D'EXPÉDITION (HORS TVA)");
    int idxTotalTax  = csvData->header.pos("TAXE TOTALE");
    int idxCurrency  = csvData->header.pos("DEVISE");
    int idxInvoice   = csvData->header.pos("ID DE FACTURE");

    // Optional columns
    static const QString COL_SHIP_SUBSIDY = "MONTANT DE LA SUBVENTION POUR L'EXPÉDITION (TVA non incluse)";
    static const QString COL_SUBSIDY_INV  = "ID DE FACTURE DE SUBVENTION";
    int idxShipSubsidy    = csvData->header.contains(COL_SHIP_SUBSIDY) ? csvData->header.pos(COL_SHIP_SUBSIDY) : -1;
    int idxSubsidyInvoice = csvData->header.contains(COL_SUBSIDY_INV)  ? csvData->header.pos(COL_SUBSIDY_INV)  : -1;
    int idxMarketplace    = csvData->header.contains("MARKETPLACE")     ? csvData->header.pos("MARKETPLACE")     : -1;

    // --- Temporary aggregation by (orderId, transactionType) ---------------------
    // Multiple CSV rows with the same orderId and type represent individual SKUs of
    // the same order; they are grouped into a single Shipment / Refund.
    struct TempOrder
    {
        QString orderId;
        QString type;           // "sales" or "return"
        QDate   date;
        QList<Activity> activities;
        QString invoiceId;
        QString subsidyInvoiceId;
    };
    QMap<QString, TempOrder> tempOrders; // key = orderId + "|" + type

    TaxResolver taxResolver(m_workingDirectory);

    for (const auto &line : csvData->lines)
    {
        if (line.isEmpty())
            continue;

        QString orderId = line.value(idxOrderId).trimmed();
        QString type    = line.value(idxType).trimmed().toLower(); // "sales" or "return"
        QString sku     = line.value(idxSku).trimmed();

        if (orderId.isEmpty())
            continue;
        if (type != "sales" && type != "return")
            continue;
        if (sku.isEmpty())
            continue;

        // --- Store (MARKETPLACE column) ------------------------------------------
        if (idxMarketplace >= 0) {
            const QString marketplace = line.value(idxMarketplace).trimmed();
            if (!marketplace.isEmpty())
                result.orderInfos->orderId_infos[orderId] = OrderManager::OrderInfo{"temu." + marketplace.toLower(), isGroupedOrders(), ""};
        }

        // --- Date ----------------------------------------------------------------
        QDate date = parseTemuVatDate(line.value(idxDate).trimmed());
        if (!date.isValid())
        {
            qWarning() << "ImporterFileTemuVatEu: skipping row with invalid date:"
                       << line.value(idxDate) << "orderId:" << orderId;
            continue;
        }

        // --- Countries / currency ------------------------------------------------
        QString departure = line.value(idxDepart).trimmed();
        QString arrival   = line.value(idxArrival).trimmed();
        QString currency  = line.value(idxCurrency).trimmed();
        if (currency.isEmpty())
            currency = "EUR";

        // --- Tax context (scheme, declaring country, VAT-paid-to) -----------------
        TaxResolver::TaxContext taxCtx = taxResolver.getTaxContext(
            date.startOfDay(),
            departure,
            arrival,
            SaleType::Products,
            false   // isCompany: Temu is a B2C marketplace
        );

        // --- Amounts -------------------------------------------------------------
        double itemExcl    = parseEuropeanAmount(line.value(idxItemExcl));
        double subsidyExcl = parseEuropeanAmount(line.value(idxSubsidy));
        double shipSubsidy = (idxShipSubsidy >= 0)
                             ? parseEuropeanAmount(line.value(idxShipSubsidy)) : 0.0;
        double shippingExcl = parseEuropeanAmount(line.value(idxShipping));
        double totalTax    = parseEuropeanAmount(line.value(idxTotalTax));

        double totalExcl = itemExcl + subsidyExcl + shipSubsidy + shippingExcl;
        double totalIncl = totalExcl + totalTax;

        // --- Invoice IDs ---------------------------------------------------------
        QString invoiceId      = line.value(idxInvoice).trimmed();
        if (invoiceId.isEmpty())
            continue;
        QString subsidyInvId   = (idxSubsidyInvoice >= 0)
                                 ? line.value(idxSubsidyInvoice).trimmed() : "";

        // --- Create Activity -----------------------------------------------------
        // activityId = orderId + "_" + sku to be unique within the order.
        ::Amount amount(totalIncl, totalTax);
        Q_ASSERT(qAbs(totalIncl) < 0.001 || qAbs(totalTax) > 0.001 ); // It could be business customer to handle

        auto actResult = Activity::create(
            orderId,                  // eventId (order number)
            orderId + "__" + departure,      // One activity per order as we don't know how to differenciate double shipment
            sku,                       // subId
            date.startOfDay(),        // dateTime
            date.startOfDay(),        // dateTimeTax (VAT date = payment date)
            currency,
            departure,                // countryCodeFrom
            arrival,                  // countryCodeTo
            false,                          // isCompany (Temu is B2C marketplace)
            taxCtx.countryCodeVatPaidTo,
            amount,
            TaxSource::MarketplaceProvided,
            taxCtx.taxDeclaringCountryCode,
            taxCtx.taxScheme,
            taxCtx.taxJurisdictionLevel,
            SaleType::Products,
            "", "",                   // territories
            invoiceId
        );

        if (!actResult.ok())
        {
            qWarning() << "ImporterFileTemuVatEu: failed to create Activity for orderId:"
                       << orderId << "sku:" << sku
                       << (actResult.errors.isEmpty() ? QString() : actResult.errors.first().message);
            continue;
        }

        // --- Accumulate into TempOrder ------------------------------------------
        QString key = orderId + "|" + type;
        TempOrder &to = tempOrders[key];
        to.orderId = orderId;
        to.type    = type;
        if (!to.date.isValid())
            to.date = date;
        to.activities.append(actResult.value.value());
        if (to.invoiceId.isEmpty() && !invoiceId.isEmpty())
            to.invoiceId = invoiceId;
        if (to.subsidyInvoiceId.isEmpty() && !subsidyInvId.isEmpty())
            to.subsidyInvoiceId = subsidyInvId;

        // --- Update global date range --------------------------------------------
        if (result.orderInfos->dateMin.isNull() || date < result.orderInfos->dateMin)
            result.orderInfos->dateMin = date;
        if (result.orderInfos->dateMax.isNull() || date > result.orderInfos->dateMax)
            result.orderInfos->dateMax = date;
    }

    // --- Convert TempOrders to Shipments / Refunds with InvoicingInfo -----------
    for (auto it = tempOrders.cbegin(); it != tempOrders.cend(); ++it)
    {
        const TempOrder &to = it.value();

        if (to.type == "sales")
        {
            Shipment shipment(to.activities, "", isGroupedOrders());
            result.orderInfos->shipments.append(shipment);

            auto infoRes = InvoicingInfo::create(
                &result.orderInfos->shipments.last(),
                {},
                to.invoiceId,
                "",
                to.date
            );
            if (infoRes.ok())
            {
                result.orderInfos->invoicingInfos.append({result.orderInfos->shipments.last().getId(), *infoRes.value});
            }
            else if (!to.invoiceId.isEmpty())
            {
                QString err = infoRes.errors.isEmpty()
                              ? "Unknown error" : infoRes.errors.first().message;
                qWarning() << "ImporterFileTemuVatEu: InvoicingInfo creation failed for SALES"
                           << to.orderId << ":" << err;
            }
        }
        else if (to.type == "return")
        {
            Refund refund(to.activities, "", isGroupedOrders());
            result.orderInfos->refunds.append(refund);

            auto infoRes = InvoicingInfo::create(
                &result.orderInfos->refunds.last(),
                {},
                to.invoiceId,
                "",
                to.date
            );
            if (infoRes.ok())
            {
                result.orderInfos->invoicingInfos.append({result.orderInfos->refunds.last().getId(), *infoRes.value});
            }
            else if (!to.invoiceId.isEmpty())
            {
                QString err = infoRes.errors.isEmpty()
                              ? "Unknown error" : infoRes.errors.first().message;
                qWarning() << "ImporterFileTemuVatEu: InvoicingInfo creation failed for RETURN"
                           << to.orderId << ":" << err;
            }
        }
    }

    co_return result;
}
