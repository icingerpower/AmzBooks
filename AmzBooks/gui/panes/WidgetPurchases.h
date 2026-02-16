#ifndef WIDGETPURCHASES_H
#define WIDGETPURCHASES_H

#include <QWidget>
#include <QFileSystemModel>

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

public:
    double getShippingPrice(const QString &countryCode) const;

private:
    Ui::WidgetPurchases *ui;
    QFileSystemModel *m_fileModel;
    QString m_currentDir;
};

#endif // WIDGETPURCHASES_H
