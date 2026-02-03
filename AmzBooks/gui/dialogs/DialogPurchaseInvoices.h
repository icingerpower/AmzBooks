#ifndef DIALOGPURCHASEINVOICES_H
#define DIALOGPURCHASEINVOICES_H

#include <QDialog>
#include <QList>
#include <QStringList>
#include "books/PurchaseInvoiceManager.h"

namespace Ui {
class DialogPurchaseInvoices;
}

class DialogPurchaseInvoices : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPurchaseInvoices(const QStringList &filePaths, QWidget *parent = nullptr);
    ~DialogPurchaseInvoices();

    QList<PurchaseInformation> selectedInvoices() const;

private:
    Ui::DialogPurchaseInvoices *ui;
    QStringList m_filePaths;
    QList<PurchaseInformation> m_validInvoices;
    
    void _setupTable();
    void _populateTable();
};

#endif // DIALOGPURCHASEINVOICES_H
