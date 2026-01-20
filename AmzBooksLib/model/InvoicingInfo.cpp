#include "InvoicingInfo.h"
#include "Shipment.h"

InvoicingInfo::InvoicingInfo(
        const Shipment *shipmentOrRefund
        , QList<LineItem> invoiceLineItems
        , std::optional<QString> invoiceNumber
        , std::optional<QString> invoiceLink)
    : m_items(invoiceLineItems)
    , m_invoiceNumber(invoiceNumber)
    , m_invoiceLink(invoiceLink)
{
    // If invoice items are empty, take from shipment? 
    // Usually InvoicingInfo might start with items from shipment if empty passed.
    // But the prompt implied moving logic.
    // If items passed are meant to be the ones on invoice, we adjust them to match Shipment Activity taxes potentially?
    // Wait, Shipment::adjustItemTaxes matched Items to Activity.
    // Now InvoicingInfo::adjustItemTaxes should likely match InvoicingInfo::m_items to shipmentOrRefund->getActivity().
    
    // Perform adjustment
    if (shipmentOrRefund) {
        adjustItemTaxes(&shipmentOrRefund->getActivity());
    }
}

void InvoicingInfo::adjustItemTaxes(const Activity *activity)
{
    if (!m_items.isEmpty()) {
        double totalItemTaxes = 0.0;
        for (const auto& item : m_items) {
            totalItemTaxes += item.getTotalTaxes();
        }

        double activityTaxes = activity->getAmountTaxes();
        double delta = activityTaxes - totalItemTaxes;

        if (qAbs(delta) > 0.0001) {// Using a small epsilon
            // Check if the adjustment on the first item (per unit) would be too large
            LineItem& firstItem = m_items[0];
            double firstTotalQty = firstItem.getQuantity(); // double conversion implied or checked

            // Avoid division by zero
            if (firstTotalQty == 0) {
                return;
            }

            double deltaPerUnitFirst = delta / firstTotalQty;

            if (qAbs(deltaPerUnitFirst) > 0.015) {
                // Spread evenly across all items based on quantity (per-unit adjustment)
                double totalQty = 0.0;
                for (const auto& item : m_items) {
                    totalQty += item.getQuantity();
                }

                if (totalQty > 0) {
                    double perUnitAdjustment = delta / totalQty;
                    for (auto& item : m_items) {
                        // Each item's unit tax is adjusted by perUnitAdjustment
                        item.adjustTaxes(perUnitAdjustment);
                    }
                }
            } else {
                // Small delta, just dump it on the first item
                firstItem.adjustTaxes(deltaPerUnitFirst);
            }
        }
    }
}

void InvoicingInfo::setItems(const Activity *activity, const QList<LineItem> &items)
{
    m_items = items;
    adjustItemTaxes(activity);
}

const QList<LineItem> &InvoicingInfo::getItems() const
{
    return m_items;
}
