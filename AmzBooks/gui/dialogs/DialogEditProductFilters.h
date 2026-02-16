#ifndef DIALOGEDITPRODUCTFILTERS_H
#define DIALOGEDITPRODUCTFILTERS_H

#include <QDialog>

class ProductFilterTable;

namespace Ui {
class DialogEditProductFilters;
}

class DialogEditProductFilters : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditProductFilters(ProductFilterTable *table, QWidget *parent = nullptr);
    ~DialogEditProductFilters();

private slots:
    void onAdd();
    void onRemove();

private:
    Ui::DialogEditProductFilters *ui;
    ProductFilterTable *m_table;
};

#endif // DIALOGEDITPRODUCTFILTERS_H
