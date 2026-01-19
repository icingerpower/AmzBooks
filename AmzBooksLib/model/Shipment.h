#ifndef SHIPMENT_H
#define SHIPMENT_H

#include "Activity.h"
#include "LineItem.h"

// Shipment = a business shipment event containing one aggregated Activity (overall net/VAT context)
// plus detailed LineItems, with a reconciliation step to minimally adjust item VAT so the sum
// matches the Activity totals.

class Shipment
{
public:
    Shipment(Activity activity, QList<LineItem> items);

    const Activity& getActivity() const noexcept;
    const QList<LineItem>& getItems() const noexcept;


protected:
    Activity m_activity;
    QList<LineItem> m_items;
    void adjustItemTexes(); // if taxes of items different of taxes of activity, a minimal delta adjustment is done
};

#endif // SHIPMENT_H
