#ifndef DIALOGVIEWSHIPMENTS_H
#define DIALOGVIEWSHIPMENTS_H

#include <QDialog>
#include <QSharedPointer>
#include "orders/OrderManager.h"

namespace Ui {
class DialogViewShipments;
}

class DialogViewShipments : public QDialog
{
    Q_OBJECT

public:
    explicit DialogViewShipments(const QList<OrderManager::ShipmentRefundsWithUpdates> &entries,
                                 int year,
                                 const OrderManager *orderManager,
                                 QWidget *parent = nullptr);
    ~DialogViewShipments();

private:
    Ui::DialogViewShipments *ui;
    class QStandardItemModel *m_model;
};

#endif // DIALOGVIEWSHIPMENTS_H
