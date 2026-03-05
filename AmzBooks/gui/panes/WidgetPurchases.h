#ifndef WIDGETPURCHASES_H
#define WIDGETPURCHASES_H

#include <QWidget>
#include <QFileSystemModel>
#include <QSharedPointer>

class ImportPriceTable;
class InventoryInvoicesTree;

namespace Ui {
class WidgetPurchases;
}

/**
 * @brief Widget that lets the user select purchase CSV files and configure
 *        per-country shipping prices.
 *
 * All WidgetPurchases instances in the application share a single
 * ImportPriceTable model, so a price change in one widget is instantly
 * reflected in every other widget.
 */
class WidgetPurchases : public QWidget
{
    Q_OBJECT

public:
    explicit WidgetPurchases(QWidget *parent = nullptr);
    ~WidgetPurchases();

    QStringList getCsvFilePaths() const;
    QDir        getPurchaseDir() const;

    /** Returns the shipping price for the given year and country code (delegates to the
     *  shared ImportPriceTable). Falls back to the Default price when the
     *  country is not found. */
    double getShippingPrice(int year, const QString &countryCode) const;

private slots:
    void selectFolder();
    void editColumns();
    void checkFiles();
    void onFolderChanged(const QString &path);
    void _connectSlots();

    /** Reads current prices from the shared model and updates the spin boxes
     *  without triggering further model writes (signals are blocked). */
    void _refreshShippingSpinBoxes();

    void addExtraPurchase();
    void removeExtraPurchase();

private:
    Ui::WidgetPurchases *ui;
    QFileSystemModel    *m_fileModel;
    QString              m_currentDir;
    InventoryInvoicesTree *m_invoicesTree;

    /** Shared across all WidgetPurchases instances in the process.
     *  Created (or re-created when the working directory changes) the first
     *  time a WidgetPurchases is constructed in a given working directory. */
    static QSharedPointer<ImportPriceTable> s_importPriceTable;
    static QString                          s_importPriceTableDir;
};

#endif // WIDGETPURCHASES_H
