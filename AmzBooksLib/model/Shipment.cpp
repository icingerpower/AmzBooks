#include "Shipment.h"

Shipment::Shipment(Activity activity)
    : m_activity(std::move(activity))
{
}

Shipment::Shipment(Activity activity, QList<LineItem> items)
    : m_activity(std::move(activity))
    , m_items(std::move(items))
{
    adjustItemTexes();
}

const QString &Shipment::getId() const noexcept
{
    return m_activity.getActivityId();
}

void Shipment::setAddressFrom(Address addressFrom) noexcept
{
    m_addressFrom = addressFrom;
}

const Activity& Shipment::getActivity() const noexcept
{
    return m_activity;
}

const QList<LineItem>& Shipment::getItems() const noexcept
{
    return m_items;
}

void Shipment::setItems(const QList<LineItem> &newItems)
{
    m_items = newItems;
    adjustItemTexes();
}

void Shipment::adjustItemTexes()
{
    if (!m_items.isEmpty()) {
        double totalItemTaxes = 0.0;
        for (const auto& item : m_items) {
            totalItemTaxes += item.getTotalTaxes();
        }

        double activityTaxes = m_activity.getAmountTaxes();
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

const std::optional<Address> &Shipment::addressFrom() const noexcept
{
    return m_addressFrom;
}
