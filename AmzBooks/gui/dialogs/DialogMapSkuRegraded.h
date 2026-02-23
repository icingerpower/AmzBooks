#ifndef DIALOGMAPSKUREGRADED_H
#define DIALOGMAPSKUREGRADED_H

#include <QDialog>

class SkuRegradedTable;

namespace Ui {
class DialogMapSkuRegraded;
}

class DialogMapSkuRegraded : public QDialog
{
    Q_OBJECT

public:
    explicit DialogMapSkuRegraded(SkuRegradedTable *skuRegradedTable,
                                  QWidget *parent = nullptr);
    ~DialogMapSkuRegraded();

private:
    Ui::DialogMapSkuRegraded *ui;
};

#endif // DIALOGMAPSKUREGRADED_H
