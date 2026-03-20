#ifndef DIALOGADDSALESERVICE_H
#define DIALOGADDSALESERVICE_H

#include "books/ServiceClientManager.h"
#include "books/ServiceSalesBooksTable.h"
#include <QDialog>
#include <QDate>

namespace Ui {
class DialogAddSaleService;
}

class DialogAddSaleService : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddSaleService(ServiceClientManager *clientManager, QWidget *parent = nullptr);
    ~DialogAddSaleService();

    // Setters for pre-filling
    void setDate(const QDate &date);
    void setReference(const QString &ref);
    void setClientByServiceLabel(const QString &label);
    void setVatOnPayment(bool vop);
    void setPaymentTermFromString(const QString &term);
    void setLineItems(const QList<ServiceSalesBooksTable::SaleLineItemInput> &items);
    // Pre-fills the unit price of the first article row (used when opening from a bank entry)
    void setFirstArticleUnitPrice(double price);

    // Getters
    QString getSelectedClientName() const;
    int     getSelectedClientRow() const;
    QDate   getDate() const;
    QString getInvoiceId() const;
    QString getCurrency() const;
    QString getAccount() const;
    PaymentType getPaymentType() const;
    int     getPaymentDays() const;
    bool    getVatOnPayment() const;

    // Returns one entry per article row with non-empty title and positive price/qty
    QList<ServiceSalesBooksTable::SaleLineItemInput> getLineItems() const;

private slots:
    void _updateCurrency();
    void _updateOkButton();
    void _updatePaymentDays();
    void _addArticle();
    void _removeArticle();
    void _onTableDataChanged();

private:
    Ui::DialogAddSaleService *ui;
    ServiceClientManager *m_clientManager;

    void _setupConnections();
    void _setupTable();
    void _addArticleRow(const QString &title = {}, double unitPrice = 0.0, double qty = 1.0);
    void _updateTotal();
};

#endif // DIALOGADDSALESERVICE_H
