#include "DialogDisplaySkus.h"
#include "ui_DialogDisplaySkus.h"

#include <QAbstractTableModel>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

// ---------------------------------------------------------------------------
// Internal read-only model — flags() advertises ItemIsEditable so the default
// delegate opens an inline editor (enabling select-all + copy), but setData()
// is intentionally NOT overridden so edits are silently discarded.
// ---------------------------------------------------------------------------
class NoPriceSkuModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Columns { COL_SKU = 0, COL_COUNT };

    explicit NoPriceSkuModel(const QStringList &skus, QObject *parent = nullptr)
        : QAbstractTableModel(parent), m_skus(skus) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid()) return 0;
        return m_skus.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid()) return 0;
        return COL_COUNT;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid()) return QVariant();
        if (role == Qt::DisplayRole || role == Qt::EditRole)
            return m_skus.at(index.row());
        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
        if (section == COL_SKU) return tr("SKU");
        return QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid()) return Qt::NoItemFlags;
        // ItemIsEditable lets the default delegate open an inline editor so the
        // user can select and copy the cell text. setData() is not overridden,
        // so the base-class implementation returns false and no change is stored.
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }

    // setData() deliberately NOT overridden.

private:
    QStringList m_skus;
};

#include "DialogDisplaySkus.moc"

// ---------------------------------------------------------------------------
// DialogDisplaySkus
// ---------------------------------------------------------------------------

DialogDisplaySkus::DialogDisplaySkus(const QStringList &skus, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogDisplaySkus)
{
    ui->setupUi(this);

    auto *model = new NoPriceSkuModel(skus, this);
    ui->tableView->setModel(model);
    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setStretchLastSection(true);

    connect(ui->buttonExport, &QPushButton::clicked, this, &DialogDisplaySkus::onExport);
}

DialogDisplaySkus::~DialogDisplaySkus()
{
    delete ui;
}

void DialogDisplaySkus::onExport()
{
    const QString path = QFileDialog::getSaveFileName(
            this, tr("Export SKUs"), QString(),
            tr("CSV Files (*.csv);;All Files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"),
                tr("Could not open file for writing:\n%1").arg(path));
        return;
    }

    QTextStream out(&file);
    const auto *model = ui->tableView->model();
    for (int row = 0; row < model->rowCount(); ++row)
        out << model->data(model->index(row, NoPriceSkuModel::COL_SKU)).toString() << "\n";
}
