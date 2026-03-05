#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "inventory/InventoryInvoicesTree.h"

#include "WidgetPurchases.h"
#include "ui_WidgetPurchases.h"
#include "gui/dialogs/DialogEditCsvPurchases.h"
#include "profit/PurchaseFileSettingsTree.h"
#include "books/ImportPriceTable.h"
#include <QFileDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QDebug>
#include <QMessageBox>
#include <QTextStream>
#include <QDir>
#include <QDirIterator>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QDate>


struct ErrorEntry {
    QString file;
    QString error;
};

// ── Static members ────────────────────────────────────────────────────────────

QSharedPointer<ImportPriceTable> WidgetPurchases::s_importPriceTable;
QString                          WidgetPurchases::s_importPriceTableDir;

// ── Construction / destruction ────────────────────────────────────────────────

WidgetPurchases::WidgetPurchases(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WidgetPurchases),
    m_invoicesTree(nullptr),
    m_fileModel(new QFileSystemModel(this))
{
    ui->setupUi(this);

    m_fileModel->setNameFilters(QStringList() << "*.csv" << "*.CSV");
    m_fileModel->setNameFilterDisables(false);

    ui->treeViewCsvFiles->setModel(m_fileModel);
    ui->treeViewCsvFiles->setAnimated(false);
    ui->treeViewCsvFiles->setIndentation(20);
    ui->treeViewCsvFiles->setSortingEnabled(true);

    // Load saved folder
    QSettings settings;
    m_currentDir = settings.value("purchases/lastFolder").toString();

    if (!m_currentDir.isEmpty() && QDir(m_currentDir).exists()) {
        onFolderChanged(m_currentDir);
    } else {
        m_currentDir.clear();
        ui->treeViewCsvFiles->setModel(nullptr);
    }

    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    m_invoicesTree = new InventoryInvoicesTree(workingDir, this);
    ui->treeViewExtraPurchases->setModel(m_invoicesTree);
    ui->treeViewExtraPurchases->setHeaderHidden(true);

    // ── Shared ImportPriceTable ───────────────────────────────────────────────
    // All WidgetPurchases instances share one model. Re-create it only when
    // the working directory has changed (e.g. the user opened a different one).
    const QString workingDirPath = workingDir.absolutePath();
    if (!s_importPriceTable || s_importPriceTableDir != workingDirPath) {
        s_importPriceTable    = QSharedPointer<ImportPriceTable>::create(workingDir);
        s_importPriceTableDir = workingDirPath;

        // First run with this working directory: migrate shipping prices that
        // were previously stored in QSettings by the old implementation.
        if (s_importPriceTable->wasNewlyCreated()) {
            auto settings = WorkingDirectoryManager::instance()->settings();
            const QList<QPair<QString, QString>> migrations = {
                {"",   "purchases/shipping_default"},
                {"US", "purchases/shipping_us"},
                {"CA", "purchases/shipping_ca"},
                {"UK", "purchases/shipping_uk"},
                {"JP", "purchases/shipping_jp"},
            };
            for (const auto &[code, key] : migrations) {
                if (settings->contains(key))
                    s_importPriceTable->setShippingPrice(0, code, settings->value(key).toDouble());
            }
        }
    }

    // Populate spin boxes from the model before connecting signals to avoid
    // triggering a write-back on the initial setValue() calls.
    ui->spinBoxYear->setValue(QDate::currentDate().year());
    _refreshShippingSpinBoxes();

    _connectSlots();
}

WidgetPurchases::~WidgetPurchases()
{
    delete ui;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void WidgetPurchases::_connectSlots()
{
    connect(ui->buttonAddPurchase,
            &QPushButton::clicked,
            this,
            &WidgetPurchases::addExtraPurchase);
    connect(ui->buttonRemovePurchase,
            &QPushButton::clicked,
            this,
            &WidgetPurchases::removeExtraPurchase);

    connect(ui->buttonSelectFolder, &QPushButton::clicked, this, &WidgetPurchases::selectFolder);
    connect(ui->buttonEditCsv, &QPushButton::clicked, this, &WidgetPurchases::editColumns);
    connect(ui->pushButton, &QPushButton::clicked, this, &WidgetPurchases::checkFiles);

    // Spin box → shared model: write the new price to the model whenever the
    // user edits a spin box.  The model then emits dataChanged, which causes
    // _refreshShippingSpinBoxes() to run on every WidgetPurchases instance.
    connect(ui->spinBoxYear, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &WidgetPurchases::_refreshShippingSpinBoxes);
    connect(ui->spinBoxShippingDefault, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ s_importPriceTable->setShippingPrice(ui->spinBoxYear->value(), "",   v); });
    connect(ui->spinBoxShippingUS, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ s_importPriceTable->setShippingPrice(ui->spinBoxYear->value(), "US", v); });
    connect(ui->spinBoxShippingCA, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ s_importPriceTable->setShippingPrice(ui->spinBoxYear->value(), "CA", v); });
    connect(ui->spinBoxShippingUK, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ s_importPriceTable->setShippingPrice(ui->spinBoxYear->value(), "UK", v); });
    connect(ui->spinBoxShippingJP, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ s_importPriceTable->setShippingPrice(ui->spinBoxYear->value(), "JP", v); });

    // Shared model → spin boxes: refresh whenever any price changes, regardless
    // of which WidgetPurchases instance triggered the change.
    connect(s_importPriceTable.get(), &ImportPriceTable::pricesChanged,
            this, [this]{ _refreshShippingSpinBoxes(); });
}

void WidgetPurchases::_refreshShippingSpinBoxes()
{
    // Block signals on all spin boxes so that programmatic setValue() calls do
    // not feed back into the model and create an infinite update loop.
    const QSignalBlocker b1(ui->spinBoxShippingDefault);
    const QSignalBlocker b2(ui->spinBoxShippingUS);
    const QSignalBlocker b3(ui->spinBoxShippingCA);
    const QSignalBlocker b4(ui->spinBoxShippingUK);
    const QSignalBlocker b5(ui->spinBoxShippingJP);

    int year = ui->spinBoxYear->value();
    ui->spinBoxShippingDefault->setValue(s_importPriceTable->getShippingPrice(year, ""));
    ui->spinBoxShippingUS->setValue(s_importPriceTable->getShippingPrice(year, "US"));
    ui->spinBoxShippingCA->setValue(s_importPriceTable->getShippingPrice(year, "CA"));
    ui->spinBoxShippingUK->setValue(s_importPriceTable->getShippingPrice(year, "UK"));
    ui->spinBoxShippingJP->setValue(s_importPriceTable->getShippingPrice(year, "JP"));
}

void WidgetPurchases::addExtraPurchase()
{
    QSettings settings;
    QString lastDir = settings.value("inventory/lastExtraPurchaseDir", QDir::homePath()).toString();

    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Select Invoice"),
                                                    lastDir,
                                                    tr("CSV Files (*.csv *.CSV)"));

    if (filePath.isEmpty()) {
        return;
    }

    settings.setValue("inventory/lastExtraPurchaseDir", QFileInfo(filePath).absolutePath());

    m_invoicesTree->addFile(filePath);
}

void WidgetPurchases::removeExtraPurchase()
{
    QModelIndex index = ui->treeViewExtraPurchases->currentIndex();
    if (!index.isValid()) {
         QMessageBox::warning(this, tr("No Selection"), tr("Please select a file to remove."));
         return;
    }

    if (m_invoicesTree->data(index, Qt::UserRole).toString().isEmpty()) {
        QMessageBox::warning(this, tr("Remove"), tr("Please select a valid file item."));
        return;
    }

    m_invoicesTree->removeFile(index);
}

void WidgetPurchases::selectFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Purchase Folder"),
                                                    m_currentDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        onFolderChanged(dir);

        QSettings settings;
        settings.setValue("purchases/lastFolder", m_currentDir);
    }
}

void WidgetPurchases::onFolderChanged(const QString &path)
{
    m_currentDir = path;
    ui->lineEditPurchaseFolder->setText(m_currentDir);

    m_fileModel->setRootPath(m_currentDir);
    ui->treeViewCsvFiles->setRootIndex(m_fileModel->index(m_currentDir));
}

void WidgetPurchases::editColumns()
{
    DialogEditCsvPurchases dialog(
                WorkingDirectoryManager::instance()->workingDir(), this);
    dialog.exec();
}

#include <QDirIterator>

void WidgetPurchases::checkFiles()
{
    QStringList files = getCsvFilePaths();

    if (files.isEmpty()) {
        QMessageBox::information(this
                                 , tr("Check Files")
                                 , tr("No CSV files found in the current directory or subdirectories."));
        return;
    }

    PurchaseFileSettingsTree settingsTree(
                WorkingDirectoryManager::instance()->workingDir());
    QList<ErrorEntry> errors;

    foreach (const QString &filePath, files) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            errors.append({QDir(m_currentDir).relativeFilePath(filePath), "Cannot open file."});
            continue;
        }

        QTextStream in(&file);
        QString headerLine = in.readLine();
        file.close();

        if (headerLine.isEmpty()) {
            errors.append({QDir(m_currentDir).relativeFilePath(filePath), "Empty header."});
            continue;
        }

        QString separator = ";";
        if (headerLine.contains("\t")) separator = "\t";
        else if (headerLine.count(",") > headerLine.count(";")) separator = ",";

        QStringList headers = headerLine.split(separator);

        for (QString &h : headers) h = h.trimmed();

        QStringList missingCols;
        for (const QString &fixedId : PurchaseFileSettingsTree::FIXED_ROW_IDS) {
            if (settingsTree.getColPos(headers, fixedId) == -1) {
                missingCols << fixedId;
            }
        }

        if (!missingCols.isEmpty()) {
            errors.append({QDir(m_currentDir).relativeFilePath(filePath), "Missing columns: " + missingCols.join(", ") + " (" + headerLine + ")"});
        }
    }

    if (errors.isEmpty()) {
        QMessageBox::information(this, tr("Check Files"), tr("All files checked successfully. No missing columns found."));
    } else {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Check Results"));
        dialog.resize(800, 400);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        QTableWidget *table = new QTableWidget(&dialog);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels(QStringList() << tr("File") << tr("Issue"));
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->setRowCount(errors.size());

        for (int i = 0; i < errors.size(); ++i) {
            table->setItem(i, 0, new QTableWidgetItem(errors[i].file));
            table->setItem(i, 1, new QTableWidgetItem(errors[i].error));
        }

        layout->addWidget(table);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttonBox);

        dialog.exec();
    }
}

QStringList WidgetPurchases::getCsvFilePaths() const
{
    if (m_currentDir.isEmpty()) return QStringList();

    QStringList absolutePaths;
    QDirIterator it(m_currentDir, QStringList() << "*.csv" << "*.CSV", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        absolutePaths << it.next();
    }

    std::sort(absolutePaths.begin(), absolutePaths.end(), std::greater<QString>());

    return absolutePaths;
}

QDir WidgetPurchases::getPurchaseDir() const
{
    return QDir(m_currentDir);
}

double WidgetPurchases::getShippingPrice(int year, const QString &countryCode) const
{
    Q_ASSERT(s_importPriceTable);
    if (!s_importPriceTable) return 0.0;
    return s_importPriceTable->getShippingPrice(year, countryCode);
}
