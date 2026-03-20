#ifndef SERVICESALESBOOKSTABLE_H
#define SERVICESALESBOOKSTABLE_H

#include "AbstractBooksTable.h"
#include "ServiceClientManager.h"
#include <QObject>
#include <QHash>
#include <functional>

class OrderManager;
class VatResolver;
class TaxResolver;
class InvoiceGenerator;

class ServiceSalesBooksTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    static constexpr QLatin1StringView CHANNEL_SALE{"Service"};

    // Extra column indices (appended after the 9 base columns)
    static const int IND_REFERENCE      = 9;
    static const int IND_TITLE          = 10;
    static const int IND_VAT_ON_PAYMENT = 11;
    static const int IND_PAYMENT_TERM   = 12;
    explicit ServiceSalesBooksTable(
            const BooksConnections *bookConnections
            , OrderManager *orderManager
            , const QDir &workingDir
            , QObject *parent = nullptr);

    QString getId() const override;

    // Provide an InvoiceGenerator so that remove() can also clean up the
    // invoice CSV registry when a generated invoice exists.  Optional — if
    // nullptr (the default) the CSV is left untouched.
    void setInvoiceGenerator(InvoiceGenerator *generator) { m_invoiceGenerator = generator; }

    // One article line in a service sale (title, unit price TTC, quantity with 1 decimal)
    struct SaleLineItemInput {
        QString title;
        double unitPriceTaxed = 0.0;
        double quantity       = 1.0;
    };

    // Custom Methods
    void createSale(const ServiceClientManager *clientManager
                    , int clientRow
                    , const QDate &date
                    , const QString &currency
                    , const QString &orderId
                    , const QString &account
                    , const QList<SaleLineItemInput> &lineItems
                    , const VatResolver &vatResolver
                    , const TaxResolver &taxResolver
                    , PaymentType paymentType = PaymentType::EndOfNextMonth
                    , int paymentDays = 0
                    , bool vatOnPayment = true
                    , const std::function<bool()> &onMissingVatRate = nullptr);
    void load(int year) override;

    bool remove(const QString &rowId) override;

    // Returns the line items for the given rowId from the persisted InvoicingInfo.
    QList<SaleLineItemInput> getLineItems(const QString &rowId) const;

    // Creates a credit-note reversal of a published sale, then adds a new sale
    // with the supplied data (two new entries: refund + new sale).
    // Throws ExceptionWithTitleText if the sale has NOT been published, or if a
    // credit note for this sale already exists.
    void replacePublishedSale(const QString &rowId,
                              const ServiceClientManager *clientManager,
                              int clientRow,
                              const QDate &date,
                              const QString &currency,
                              const QString &newOrderId,
                              const QString &account,
                              const QList<SaleLineItemInput> &lineItems,
                              const VatResolver &vatResolver,
                              const TaxResolver &taxResolver,
                              PaymentType paymentType = PaymentType::EndOfNextMonth,
                              int paymentDays = 0,
                              bool vatOnPayment = true,
                              const std::function<bool()> &onMissingVatRate = nullptr);

    // Replaces an existing unpublished sale in-place.
    // Throws ExceptionWithTitleText if the sale has been published.
    // Respects m_invoiceGenerator: call setInvoiceGenerator() before this if an
    // invoice number must be cleaned up from the CSV registry on removal.
    void replaceSale(const QString &rowId,
                     const ServiceClientManager *clientManager,
                     int clientRow,
                     const QDate &date,
                     const QString &currency,
                     const QString &newOrderId,
                     const QString &account,
                     const QList<SaleLineItemInput> &lineItems,
                     const VatResolver &vatResolver,
                     const TaxResolver &taxResolver,
                     PaymentType paymentType = PaymentType::EndOfNextMonth,
                     int paymentDays = 0,
                     bool vatOnPayment = true,
                     const std::function<bool()> &onMissingVatRate = nullptr);

    // QAbstractTableModel overrides for the 3 extra columns
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

private:
    OrderManager *m_orderManager;
    InvoiceGenerator *m_invoiceGenerator = nullptr;

    // rowId → {reference, title, vatOnPayment (bool), paymentTerm (QString)}
    QHash<QString, QVariantList> m_extraData;

    void _setExtra(const QString &rowId, const QString &reference, const QString &title, bool vatOnPayment, const QString &paymentTerm);
    static QString _paymentTermStr(const QDate &orderDate, const QDate &paymentDate);

    // Creates a credit-note entry mirroring rowId with all amounts negated,
    // stored under refundOrderId.
    void _createRefundEntry(const QString &rowId, const QString &refundOrderId);
};

#endif // SERVICESALESBOOKSTABLE_H
