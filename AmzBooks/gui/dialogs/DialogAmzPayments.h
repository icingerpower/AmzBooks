#ifndef DIALOGAMZPAYMENTS_H
#define DIALOGAMZPAYMENTS_H

#include <QDialog>
#include <QList>
#include <QStringList>
#include "books/PurchaseAmzPaymentsManager.h"

namespace Ui {
class DialogAmzPayments;
}

class DialogAmzPayments : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAmzPayments(const QStringList &filePaths, QWidget *parent = nullptr);
    ~DialogAmzPayments();

    QList<AmzPaymentInfo> selectedPayments() const;

private:
    Ui::DialogAmzPayments *ui;
    QStringList m_filePaths;
    QList<AmzPaymentInfo> m_validPayments;

    void _setupTable();
    void _populateTable();
};

#endif // DIALOGAMZPAYMENTS_H
