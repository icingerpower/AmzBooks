#ifndef WIDGETPURCHASES_H
#define WIDGETPURCHASES_H

#include <QWidget>
#include <QFileSystemModel>
#include <QSharedPointer>

class ImportPriceTable;

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

    bool isInitialized() const;
    QStringList getCsvFilePaths() const;
    QDir        getPurchaseDir() const;

    /** Returns CSV file paths from the inventory folder, optionally filtered by
     *  year (files whose name starts with "YYYY-"). Pass 0 to return all files. */
    QStringList getCsvFilePathsInventory(int year) const;

    /** Returns the shipping price for the given year and country code (delegates to the
     *  shared ImportPriceTable). Falls back to the Default price when the
     *  country is not found. */
    double getShippingPrice(int year, const QString &countryCode) const;

private slots:
    void selectFolder();
    void selectInventoryFolder();
    void editColumns();
    void checkFiles();
    void onFolderChanged(const QString &path);
    void onInventoryFolderChanged(const QString &path);
    void _connectSlots();

    /** Reads current prices from the shared model and updates the spin boxes
     *  without triggering further model writes (signals are blocked). */
    void _refreshShippingSpinBoxes();

private:
    Ui::WidgetPurchases *ui;
    QFileSystemModel    *m_fileModel;
    QFileSystemModel    *m_fileModelInventory;
    QString              m_currentDir;
    QString              m_inventoryDir;

    /** Shared across all WidgetPurchases instances in the process.
     *  Created (or re-created when the working directory changes) the first
     *  time a WidgetPurchases is constructed in a given working directory. */
    static QSharedPointer<ImportPriceTable> s_importPriceTable;
    static QString                          s_importPriceTableDir;
};

#endif // WIDGETPURCHASES_H
