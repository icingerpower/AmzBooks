#ifndef DIALOGPICKSHIPMENT_H
#define DIALOGPICKSHIPMENT_H

#include <QDialog>
#include <QSharedPointer>

namespace Ui {
class DialogPickShipment;
}

class Shipment;

class DialogPickShipment : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPickShipment(const QString &errorTitle,
                                const QString &errorText,
                                const QList<QSharedPointer<Shipment>> &shipments,
                                QWidget *parent = nullptr);
    ~DialogPickShipment();

    QString selectedShipmentId() const;

private:
    Ui::DialogPickShipment *ui;
    QList<QSharedPointer<Shipment>> m_shipments;
    QString m_selectedId;
};

#endif // DIALOGPICKSHIPMENT_H
