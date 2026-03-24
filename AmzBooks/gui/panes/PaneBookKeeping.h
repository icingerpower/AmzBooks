#ifndef PANEBOOKKEEPING_H
#define PANEBOOKKEEPING_H

#include <QWidget>
#include <QDir>

class BooksConnections;

namespace Ui {
class PaneBookKeeping;
}

class AbstractBooksTableBank;
class AbstractBooksTable;
class EntrySelfTable;
class PurchaseInvoiceTable;
class PurchaseAmzPaymentsTable;
#include <QCoroTask>

class PaneBookKeeping : public QWidget
{
    Q_OBJECT

public:
    explicit PaneBookKeeping(QWidget *parent = nullptr);
    ~PaneBookKeeping();

private:
    Ui::PaneBookKeeping *ui;
    void _createBanks();
    void _deleteBooksTables();
    void _createBooksTables();
    void generateSaleReports(const QDir &dirTo);
    void _initYears();
    void _connectSlots();
    void _setSubButtonsEnabled(bool enable);
    void _updateServiceButtonsEnabled();
    void showEvent(QShowEvent *event) override;
    EntrySelfTable *getSeflEntryTable() const;
    PurchaseInvoiceTable *getPurchaseInvoiceTable() const;
    PurchaseAmzPaymentsTable *getAmzPaymentsTable() const;
    QList<AbstractBooksTable *> getAllBookTables() const;
    QList<AbstractBooksTableBank *> getAllBankTables() const;
    QList<AbstractBooksTable *> getAllNonBankTables() const;
    AbstractBooksTableBank *getVisibleBankTable() const;

#include <optional>
#include <QSet>

public slots:
    void loadYearSelected();
    void generateBookKeeping();
    void generateReports();
    void generateInvoices();
    void regenerateInvoices();
    void unselectAll();
    void associate();
    void dissociate();

    void selfEntryAdd();
    void selfEntryRemove();

    void purchaseAdd();
    void purchaseAddMany();
    void purchaseRemove();

    void bankAdd();
    void bankRemove();

    void serviceAddSale();
    void serviceRemoveSale();
    void serviceEditSale();
    void serviceReInvoice();
    void serviceEditClients();
    void serviceCreateFromSelection();

    void amzPaymentAdd();
    void amzPaymentAddMany();
    void amzPaymentRemove();

private:
    void generateInvoicesWithSelection(std::optional<QSet<QString>> selectedShipmentIds);
    void displayPurchaseMissingWarning();
    BooksConnections *m_booksConnections;
    class OrderManager *m_orderManager;
    bool m_splitterInitialized = false;
    QCoro::Task<> generateBookKeepingAsync();
};

#endif // PANEBOOKKEEPING_H
