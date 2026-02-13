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

private slots:
    void onSelectFolder();
    void onEditColumns();
    void onCheckFiles();
    void onFolderChanged(const QString &path);
    void _connectSlots();

private:
    Ui::WidgetPurchases *ui;
    QFileSystemModel *m_fileModel;
    QString m_currentDir;
};

#endif // WIDGETPURCHASES_H
