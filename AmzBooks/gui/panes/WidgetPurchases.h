#ifndef WIDGETPURCHASES_H
#define WIDGETPURCHASES_H

#include <QWidget>
#include <QFileSystemModel>

class InventoryInvoicesTree;

namespace Ui {
class WidgetPurchases;
}

class WidgetPurchases : public QWidget
{
    Q_OBJECT

public:
    explicit WidgetPurchases(QWidget *parent = nullptr);
    ~WidgetPurchases();

    QStringList getCsvFilePaths() const;
    QDir getPurchaseDir() const;

private slots:
    void selectFolder();
    void editColumns();
    void checkFiles();
    void onFolderChanged(const QString &path);
    void _connectSlots();
    void _saveSettings();
    void addExtraPurchase();
    void removeExtraPurchase();

public:
    double getShippingPrice(const QString &countryCode) const;

private:
    Ui::WidgetPurchases *ui;
    QFileSystemModel *m_fileModel;
    QString m_currentDir;
    InventoryInvoicesTree *m_invoicesTree;
};

#endif // WIDGETPURCHASES_H
