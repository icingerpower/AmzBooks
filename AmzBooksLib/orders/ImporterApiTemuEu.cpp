#include "ImporterApiTemuEu.h"
#include "books/Activity.h"
#include "orders/Shipment.h"
#include "orders/Amount.h"
#include "CountriesEu.h"
#include "ExceptionWithTitleText.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

DECLARE_IMPORTER_API(ImporterApiTemuEu)

QString ImporterApiTemuEu::getEndpoint() const
{
    // EU seller gateway of the Temu Open Platform router
    return "https://openapi-b-eu.temu.com";
}

QString ImporterApiTemuEu::getId() const
{
    return "TemuEu";
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchShipments(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    result.orderInfos->dateMin = QDate();
    result.orderInfos->dateMax = QDate();

    QList<ShopConfig> shops = getShops();
    QStringList errors;

    for (const auto& shop : shops) {
        try {
            co_await fetchShopOrders(shop, dateFrom, result.orderInfos);
        } catch (const std::exception& e) {
            errors.append(QString("Shop '%1' error: %2").arg(shop.name, e.what()));
        }
    }

    if (!errors.isEmpty()) {
        if (shops.size() > 1) {
             result.errorReturned = QString("Partial failure: %1").arg(errors.join("; "));
        } else {
             result.errorReturned = errors.first();
        }
    }

    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchRefunds(const QDateTime &dateFrom)
{
    // All bookable data (shipments, addresses and refund clues) is gathered in
    // _fetchShipments via the 3-call flow. The pane/CLI merge the OrderInfos of
    // all four fetch methods, so returning an empty result here is intentional.
    Q_UNUSED(dateFrom);
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchAddresses(const QDateTime &dateFrom)
{
    // See _fetchRefunds: addresses are collected in _fetchShipments.
    Q_UNUSED(dateFrom);
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchInvoiceInfos(const QDateTime &dateFrom)
{
    // See _fetchRefunds: nothing extra to gather here.
    Q_UNUSED(dateFrom);
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    co_return result;
}

bool ImporterApiTemuEu::recomputeTaxes() const
{
    return true;
}

namespace {

// One money field of a Temu response: {"amount": <int cents>, "currency": "EUR"}.
struct TemuMoney {
    double  value = 0.0; // currency units (cents / 100.0)
    QString currency;
};

// Reads a nested money object; amounts are minor units (cents) → /100.0.
TemuMoney readMoney(const QJsonObject &parent, const QString &key)
{
    const QJsonObject obj = parent.value(key).toObject();
    TemuMoney money;
    money.value = obj.value("amount").toDouble() / 100.0;
    money.currency = obj.value("currency").toString();
    return money;
}

} // namespace

QCoro::Task<void> ImporterApiTemuEu::fetchShopOrders(const ShopConfig& shop, const QDateTime& dateFrom, QSharedPointer<OrderInfos> targetInfos)
{
    // Temu Open Platform — 3-call flow per shop (protocol/transport in
    // ImporterApiTemu::sendTemuRequest; router POST + MD5 sign):
    //
    //   1. bg.order.list.v2.get   (paged) — list parent orders since dateFrom.
    //      result.pageItems[] each carries parentOrderMap {parentOrderSn,
    //      parentOrderTime} and a nested orderList of sub-orders. The
    //      bookkeeping order id is the parentOrderSn (the "PO-…" number that
    //      also appears in Temu's exported CSV reports).
    //   2. bg.order.amount.query  {parentOrderSn} — SINGULAR (a list errors).
    //      parentOrderMap money fields (cents): net = retailPriceTotalTaxExcl,
    //      tax = productTaxAmount, refunds = refundsTotal. gross = net + tax;
    //      shipping is EXCLUDED to match the CSV importer.
    //   3. bg.order.shippinginfo.v2.get {parentOrderSn} — SINGULAR — geo only:
    //      regionName1 → ISO country, postCode, regionName3 (city).
    //
    // This performs 2 extra sequential calls per parent order; that is
    // expected — do NOT parallelize (the gateway is rate-limited and the calls
    // carry PII we want to keep serialized).
    //
    // Output mirrors ImporterFileTemuOrders: one grouped Shipment per parent
    // order, plus an Address and (when refundsTotal > 0) a refund clue. Tax is
    // recomputed downstream (recomputeTaxes() == true) using the address, so
    // the initial scheme is only a placeholder.

    const qint64 nowSecs = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    // The shop's site gives the "temu.fr"-style store id used by the file importers
    const QString store = shop.country.isEmpty()
        ? QString("temu.%1").arg(shop.name.toLower())
        : QString("temu.%1").arg(shop.country.toLower());

    const int pageSize = 100;
    int       page     = 1;
    bool      hasMore  = true;

    // Per-order failures do not abort the shop; they are aggregated and raised
    // once at the end so _fetchShipments can surface them as a partial failure.
    QStringList orderErrors;

    while (hasMore) {
        QJsonObject businessParams;
        // The gateway has been observed to expect different page-key spellings
        // depending on the API version — sending all three is accepted and safe
        businessParams.insert("pageNo", page);
        businessParams.insert("pageNumber", page);
        businessParams.insert("page", page);
        businessParams.insert("pageSize", pageSize);
        businessParams.insert("createAfter", dateFrom.toUTC().toSecsSinceEpoch());
        businessParams.insert("createBefore", nowSecs);

        const QJsonObject result = co_await sendTemuRequest(shop, "bg.order.list.v2.get", businessParams);

        QJsonArray orderList = result.value("pageItems").toArray();
        if (orderList.isEmpty()) {
            orderList = result.value("orderList").toArray();
        }
        if (orderList.isEmpty()) {
            orderList = result.value("order_list").toArray();
        }

        if (orderList.isEmpty()) {
            hasMore = false;
            break;
        }

        for (const QJsonValue &parentVal : std::as_const(orderList)) {
            const QJsonObject parentObj = parentVal.toObject();
            const QJsonObject parentMap = parentObj.value("parentOrderMap").toObject();

            QString parentOrderSn = parentMap.value("parentOrderSn").toString();
            if (parentOrderSn.isEmpty()) {
                parentOrderSn = parentObj.value("parentOrderSn").toString();
            }
            if (parentOrderSn.isEmpty()) {
                // Flat structure fallback: a sub-order exposed at top level
                parentOrderSn = parentObj.value("orderSn").toString();
            }
            if (parentOrderSn.isEmpty()) {
                continue;
            }

            // Keep the orderId_infos entry for every parent order, even those
            // we later skip as cancelled/empty.
            targetInfos->orderId_infos[parentOrderSn]
                = OrderManager::OrderInfo{store, isGroupedOrders(), ""};

            const qint64 parentOrderTime = parentMap.value("parentOrderTime").toVariant().toLongLong();
            const QDateTime dt = QDateTime::fromSecsSinceEpoch(parentOrderTime);

            // 2. Amount query — SINGULAR parentOrderSn.
            QJsonObject amountParams;
            amountParams.insert("parentOrderSn", parentOrderSn);
            const QJsonObject amountResult = co_await sendTemuRequest(shop, "bg.order.amount.query", amountParams);
            const QJsonObject amountMap = amountResult.value("parentOrderMap").toObject();

            // Booked basis MUST match the existing (CSV-imported) Temu shipments.
            // Validated row-for-row against Orders.db July orders (14/14 exact):
            //   gross (amountTaxed)  = customerPaid          (what the buyer paid,
            //                          incl. shipping; stable over time)
            //   tax   (amountTaxes)  = taxTotalAfterDiscount
            // Do NOT use estimatedRevenue: it is Temu's net-of-fees estimate and
            // DRIFTS DOWNWARD after the sale as fees settle, so re-importing an
            // older month would book less than was originally declared. Do NOT use
            // retailPriceTotalTaxExcl/productTaxAmount either (omit shipping).
            const TemuMoney paid     = readMoney(amountMap, "customerPaid");
            const TemuMoney taxAfter = readMoney(amountMap, "taxTotalAfterDiscount");
            const TemuMoney refunds  = readMoney(amountMap, "refundsTotal");

            // Skip cancelled/empty parent orders (no sale and no refund) — the
            // orderId_infos entry above is kept regardless.
            if (paid.value <= 0.0 && refunds.value <= 0.0) {
                continue;
            }

            // Currency comes from the money objects; prefer the paid amount,
            // fall back to refunds for refund-only orders.
            QString currency = paid.currency;
            if (currency.isEmpty()) {
                currency = refunds.currency;
            }

            // 3. Shipping info — SINGULAR; geo fields only. Never abort the
            // import for one address: fall back to the shop's own country.
            QString destCountry = shop.country;
            QString postCode;
            QString city;
            QString stateOrRegion;
            try {
                QJsonObject shipParams;
                shipParams.insert("parentOrderSn", parentOrderSn);
                const QJsonObject shipResult = co_await sendTemuRequest(shop, "bg.order.shippinginfo.v2.get", shipParams);

                const QString regionName1 = shipResult.value("regionName1").toString();
                if (regionName1.isEmpty()) {
                    qWarning() << "Temu shippinginfo for" << parentOrderSn
                               << "has no regionName1; falling back to shop country" << shop.country;
                } else {
                    destCountry = CountriesEu::toCode(regionName1);
                    stateOrRegion = shipResult.value("regionName2").toString();
                    city = shipResult.value("regionName3").toString();
                    if (city.isEmpty()) {
                        city = stateOrRegion;
                    }
                    postCode = shipResult.value("postCode").toString();
                }
            } catch (const std::exception &e) {
                qWarning() << "Temu shippinginfo for" << parentOrderSn
                           << "failed; falling back to shop country" << shop.country
                           << ":" << e.what();
            }

            // 4. Build the grouped Shipment: ::Amount(totalIncl, totalTax).
            const QString originCountry = shop.country;
            const double gross = paid.value;
            ::Amount amount(gross, taxAfter.value);

            const TaxScheme scheme = (originCountry == destCountry)
                ? TaxScheme::DomesticVat
                : TaxScheme::EuOssUnion;

            const auto actResult = Activity::create(
                parentOrderSn,   // eventId
                parentOrderSn,   // activityId
                "",              // subActivityId
                dt,              // dateTime
                dt,              // dateTimeTax
                currency,        // currency
                originCountry,   // countryCodeFrom
                destCountry,     // countryCodeTo
                false,           // isCompany (Temu is B2C marketplace)
                destCountry,     // countryCodeVatPaidTo
                amount,          // amount (gross + tax)
                TaxSource::MarketplaceProvided,
                destCountry,     // taxDeclaringCountryCode
                scheme,
                TaxJurisdictionLevel::Country,
                SaleType::Products);

            if (!actResult.ok()) {
                orderErrors.append(QString("Activity create error for order %1").arg(parentOrderSn));
                continue;
            }

            QList<Activity> acts;
            acts.append(*actResult.value);
            Shipment shipment(acts, "", isGroupedOrders());
            targetInfos->shipments.append(shipment);

            // Address for downstream tax recompute (needs country/postal/city).
            Address address(QString{}, QString{}, QString{}, QString{},
                            city, postCode, destCountry, stateOrRegion,
                            QString{}, QString{}, QString{}, QString{});
            targetInfos->orderAddresses.append(AddressToWithId{parentOrderSn, address});

            // Refund clue when the parent order carries a refund.
            if (refunds.value > 0.0) {
                targetInfos->orderId_refundClues[parentOrderSn]
                    .append(RefundClue{refunds.value, currency, dt.date()});
            }

            // Track the booked date range across shops.
            const QDate bookedDate = dt.date();
            if (bookedDate.isValid()) {
                if (!targetInfos->dateMin.isValid() || bookedDate < targetInfos->dateMin) {
                    targetInfos->dateMin = bookedDate;
                }
                if (!targetInfos->dateMax.isValid() || bookedDate > targetInfos->dateMax) {
                    targetInfos->dateMax = bookedDate;
                }
            }
        }

        hasMore = orderList.size() >= pageSize;
        ++page;
    }

    if (!orderErrors.isEmpty()) {
        // Good shipments are already appended to targetInfos; raising here only
        // reports the per-order failures (aggregated by _fetchShipments).
        ExceptionWithTitleText ex(QStringLiteral("Temu order import errors"),
                                  orderErrors.join(QStringLiteral("; ")));
        ex.raise();
    }

    co_return;
}
