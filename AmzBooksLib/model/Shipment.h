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
    Shipment(Activity activity);
    const QString& getId() const noexcept;

    const Activity& getActivity() const noexcept;

protected:
    Activity m_activity;
};

#endif // SHIPMENT_H
