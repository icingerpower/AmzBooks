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

    // Try ISO / numeric formats: "yyyy-MM-dd", "dd.MM.yyyy", "dd/MM/yyyy"
    for (const QString &fmt : {QStringLiteral("yyyy-MM-dd"),
                               QStringLiteral("dd.MM.yyyy"),
                               QStringLiteral("dd/MM/yyyy")})
    {
        QDate date = QDate::fromString(s, fmt);
        if (date.isValid())
            return date;
    }

    // Localized abbreviated / full month names (lowercase, without trailing dot)
    // across FR, DE, EN, IT, ES:
    //   FR: "26 janv. 2026", "1 déc. 2025", "19 août 2026"
    //   DE: "21. Sept. 2026", "4. Sept. 2026", "15. März 2026"
    //   EN: "21 Sep 2026", "Sep 21, 2026"
    static const QHash<QString, int> monthMap = {
        // 1 - January
        {"janv", 1}, {"janvier", 1}, {"jan", 1}, {"januar", 1}, {"jän", 1}, {"jänner", 1},
        {"january", 1}, {"gen", 1}, {"gennaio", 1}, {"ene", 1}, {"enero", 1},
        // 2 - February
        {"févr", 2}, {"fevr", 2}, {"fév", 2}, {"fev", 2}, {"février", 2}, {"fevrier", 2},
        {"feb", 2}, {"februar", 2}, {"february", 2}, {"febbraio", 2}, {"febrero", 2},
        // 3 - March
        {"mars", 3}, {"märz", 3}, {"maerz", 3}, {"mär", 3}, {"mrz", 3},
        {"mar", 3}, {"march", 3}, {"marzo", 3},
        // 4 - April
        {"avr", 4}, {"avril", 4}, {"apr", 4}, {"april", 4}, {"aprile", 4}, {"abr", 4}, {"abril", 4},
        // 5 - May
        {"mai", 5}, {"may", 5}, {"mag", 5}, {"maggio", 5}, {"mayo", 5},
        // 6 - June
        {"juin", 6}, {"juni", 6}, {"jun", 6}, {"june", 6}, {"giu", 6}, {"giugno", 6}, {"junio", 6},
        // 7 - July
        {"juil", 7}, {"juillet", 7}, {"juli", 7}, {"jul", 7}, {"july", 7},
        {"lug", 7}, {"luglio", 7}, {"julio", 7},
        // 8 - August
        {"août", 8}, {"aout", 8}, {"aug", 8}, {"august", 8}, {"ago", 8}, {"agosto", 8},
        // 9 - September
        {"sept", 9}, {"sep", 9}, {"septembre", 9}, {"september", 9},
        {"set", 9}, {"settembre", 9}, {"septiembre", 9}, {"setiembre", 9},
        // 10 - October
        {"oct", 10}, {"octobre", 10}, {"okt", 10}, {"oktober", 10}, {"october", 10},
        {"ott", 10}, {"ottobre", 10}, {"octubre", 10},
        // 11 - November
        {"nov", 11}, {"novembre", 11}, {"november", 11}, {"noviembre", 11},
        // 12 - December
        {"déc", 12}, {"dec", 12}, {"décembre", 12}, {"decembre", 12},
        {"dez", 12}, {"dezember", 12}, {"december", 12}, {"dic", 12}, {"dicembre", 12}, {"diciembre", 12}
    };

    auto lookupMonth = [](QString token) -> int {
        token = token.trimmed().toLower();
        while (token.endsWith('.') || token.endsWith(','))
            token.chop(1);
        return monthMap.value(token, 0);
    };

    auto parseDayToken = [](QString token, bool *ok) -> int {
        token = token.trimmed();
        while (token.endsWith('.') || token.endsWith(','))
            token.chop(1);
        return token.toInt(ok);
    };

    QStringList parts = s.split(' ', Qt::SkipEmptyParts);
    if (parts.size() == 3)
    {
        bool yearOk = false;
        int year = parts[2].toInt(&yearOk);
        if (yearOk)
        {
            // Format 1: "<day>[.] <month>[.] <year>" (FR/DE/IT/ES/EN, e.g. "21. Sept. 2026", "26 janv. 2026")
            bool dayOk = false;
            int day = parseDayToken(parts[0], &dayOk);
            int month = lookupMonth(parts[1]);
            if (dayOk && month > 0)
            {
                QDate d(year, month, day);
                if (d.isValid())
                    return d;
            }

            // Format 2: "<month>[.] <day>[,] <year>" (EN, e.g. "Sep 21, 2026")
            month = lookupMonth(parts[0]);
            day = parseDayToken(parts[1], &dayOk);
            if (dayOk && month > 0)
            {
                QDate d(year, month, day);
                if (d.isValid())
                    return d;
            }
        }
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

    // --- Validate required columns & resolve indices -----------------------------
    // Raw CSV headers for departure country have a trailing space inside quotes,
    // which CsvHeader stores trimmed.
    int idxOrderId  = -1;
    int idxDate     = -1;
    int idxType     = -1;
    int idxSku      = -1;
    int idxDepart   = -1;
    int idxArrival  = -1;
    int idxItemExcl = -1;
    int idxSubsidy  = -1;
    int idxShipping = -1;
    int idxTotalTax = -1;
    int idxCurrency = -1;
    int idxInvoice  = -1;
    try
    {
        idxOrderId  = csvData->header.pos({"N° DE COMMANDE", "BESTELLNUMMER", "ORDER ID", "NUMERO D'ORDINE", "N.º DE PEDIDO"});
        idxDate     = csvData->header.pos({"DATE DE PAIEMENT DE LA COMMANDE", "ZAHLUNGSDATUM DER BESTELLUNG", "ORDER PAYMENT DATE", "DATA DI PAGAMENTO DELL'ORDINE", "FECHA DE PAGO DEL PEDIDO"});
        idxType     = csvData->header.pos({"TYPE DE VENTE", "VERKAUFSART", "SALES TYPE", "TIPO DI VENDITA", "TIPO DE VENTA"});
        idxSku      = csvData->header.pos({"SKU DU VENDEUR", "VERKÄUFER-SKU", "SELLER SKU", "SKU DEL VENDITORE", "SKU DEL VENDEDOR"});
        idxDepart   = csvData->header.pos({"PAYS DE DÉPART DE LA TRANSACTION", "HERKUNFTSLAND DER TRANSAKTION", "TRANSACTION DEPARTURE COUNTRY", "PAESE DI PARTENZA DELLA TRANSAZIONE", "PAÍS DE SALIDA DE LA TRANSACCIÓN"});
        idxArrival  = csvData->header.pos({"PAYS D'ARRIVÉE DE LA TRANSACTION", "ANKUNFTSLAND DER TRANSAKTION", "TRANSACTION ARRIVAL COUNTRY", "PAESE DI ARRIVO DELLA TRANSAZIONE", "PAÍS DE LLEGADA DE LA TRANSACCIÓN"});
        idxItemExcl = csvData->header.pos({"PRIX DES ARTICLES (HORS TVA)", "PREIS DER ARTIKEL (OHNE UST.)", "ITEM PRICE (EXCL. VAT)", "PREZZO DEGLI ARTICOLI (IVA ESCLUSA)", "PRECIO DE LOS ARTÍCULOS (IVA EXCLUIDO)"});
        idxSubsidy  = csvData->header.pos({"PRIX DE LA SUBVENTION (HORS TVA)", "ZUSCHUSSPREIS (OHNE UST.)", "SUBSIDY PRICE (EXCL. VAT)", "PREZZO DEL SUSSIDIO (IVA ESCLUSA)", "PRECIO DE LA SUBVENCIÓN (IVA EXCLUIDO)"});
        idxShipping = csvData->header.pos({"PRIX D'EXPÉDITION (HORS TVA)", "PREIS DER SENDUNG (OHNE UST.)", "SHIPPING PRICE (EXCL. VAT)", "PREZZO DI SPEDIZIONE (IVA ESCLUSA)", "PRECIO DE ENVÍO (IVA EXCLUIDO)"});
        idxTotalTax = csvData->header.pos({"TAXE TOTALE", "STEUERN INSGESAMT", "TOTAL TAX", "TOTALE IMPOSTE", "IMPUESTO TOTAL"});
        idxCurrency = csvData->header.pos({"DEVISE", "WÄHRUNG", "CURRENCY", "VALUTA", "MONEDA"});
        idxInvoice  = csvData->header.pos({"ID DE FACTURE", "RECHNUNGSNUMMER", "INVOICE NUMBER", "NUMERO FATTURA", "NÚMERO DE FACTURA"});
    }
    catch (const CsvHeaderException &e)
    {
        result.errorReturned = e.getErrorColumns("Missing column:");
        co_return result;
    }

    // Optional columns
    auto optionalPos = [&](const QStringList &names) -> int {
        try
        {
            return csvData->header.pos(names);
        }
        catch (const CsvHeaderException &)
        {
            return -1;
        }
    };
    int idxShipSubsidy    = optionalPos({"MONTANT DE LA SUBVENTION POUR L'EXPÉDITION (TVA non incluse)", "VERSANDZUSCHUSSPREIS (OHNE MWST.)", "SHIPPING SUBSIDY PRICE (EXCL. VAT)", "PREZZO DEL SUSSIDIO DI SPEDIZIONE (IVA ESCLUSA)", "PRECIO DE SUBVENCIÓN DE ENVÍO (IVA EXCLUIDO)"});
    int idxShipSubsidyVat = optionalPos({"MONTANT DE LA TVA SUR LA SUBVENTION POUR L'EXPÉDITION", "MEHRWERTSTEUERBETRAG DES VERSANDZUSCHUSSES", "SHIPPING SUBSIDY VAT AMOUNT", "IMPORTO IVA DEL SUSSIDIO DI SPEDIZIONE", "IMPORTE DE IVA DE LA SUBVENCIÓN DE ENVÍO"});
    int idxSubsidyInvoice = optionalPos({"ID DE FACTURE DE SUBVENTION", "SUBVENTIONSRECHNUNGSNUMMER", "SUBSIDY INVOICE NUMBER", "ID FATTURA SUSSIDIO", "ID DE FACTURA DE SUBVENCIÓN"});
    int idxMarketplace    = optionalPos({"MARKETPLACE", "MARKTPLATZ", "MERCATO", "MERCADO"});

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
        double itemExcl     = parseEuropeanAmount(line.value(idxItemExcl));
        double subsidyExcl  = parseEuropeanAmount(line.value(idxSubsidy));
        double shipSubsidy  = (idxShipSubsidy >= 0)
                              ? parseEuropeanAmount(line.value(idxShipSubsidy)) : 0.0;
        double shipSubsidyVat = (idxShipSubsidyVat >= 0)
                                ? parseEuropeanAmount(line.value(idxShipSubsidyVat)) : 0.0;
        double shippingExcl = parseEuropeanAmount(line.value(idxShipping));
        // TAXE TOTALE covers article + regular-shipping VAT but not shipping-subsidy VAT
        // (which appears in a separate column when Temu subsidises free shipping).
        double totalTax     = parseEuropeanAmount(line.value(idxTotalTax)) + shipSubsidyVat;

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

        const QString activityId = (type == "return")
                                   ? (orderId + "__" + departure + "__return")
                                   : (orderId + "__" + departure);

        auto actResult = Activity::create(
            orderId,                  // eventId (order number)
            activityId,               // One activity per order/type as we don't know how to differentiate double shipment
            sku,                      // subId
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
