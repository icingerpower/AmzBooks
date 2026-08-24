#ifndef IMPORTERAPITEMUEU_H
#define IMPORTERAPITEMUEU_H

#include "ImporterApiTemu.h"

class ImporterApiTemuEu : public ImporterApiTemu
{
public:
    using ImporterApiTemu::ImporterApiTemu;

protected:
    QString getEndpoint() const override;
    QString getId() const override;

    QCoro::Task<ReturnOrderInfos> _fetchShipments(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchRefunds(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchAddresses(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchInvoiceInfos(const QDateTime &dateFrom) override;
    bool recomputeTaxes() const override;

private:
    // Fetches one shop's orders and appends bookable data to targetInfos using
    // a 3-call flow per parent order:
    //   1. bg.order.list.v2.get        (paged) — list parent orders.
    //   2. bg.order.amount.query       {parentOrderSn} — net/tax/refund totals
    //      (cents → /100.0; shipping excluded to match the CSV importer).
    //   3. bg.order.shippinginfo.v2.get {parentOrderSn} — destination geo
    //      (regionName1 → ISO country, postCode, regionName3 → city); on
    //      failure/absence, falls back to the shop's own country and warns.
    // Produces one grouped Shipment per parentOrderSn plus an Address and, when
    // refundsTotal > 0, a refund clue. Calls 2 and 3 are intentionally
    // sequential (2 extra calls per parent order); do NOT parallelize.
    // Per-order failures are aggregated and raised at the end so partial
    // progress (already-appended shipments) is preserved.
    QCoro::Task<void> fetchShopOrders(const ShopConfig& shop, const QDateTime& dateFrom, QSharedPointer<OrderInfos> targetInfos);
};

#endif // IMPORTERAPITEMUEU_H
