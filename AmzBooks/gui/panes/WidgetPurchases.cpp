#include "../../common/workingdirectory/WorkingDirectoryManager.h"

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
QList<WidgetPurchases*>          WidgetPurchases::s_widgetPurchasesInstances;

// ── Construction / destruction ────────────────────────────────────────────────

 WidgetPurchases::WidgetPurchases(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WidgetPurchases),
    m_fileModel(new QFileSystemModel(this)),
    m_fileModelInventory(new QFileSystemModel(this))
{
    ui->setupUi(this);

    s_widgetPurchasesInstances.append(this);

    m_fileModel->setNameFilters(QStringList() << "*.csv" << "*.CSV");
    m_fileModel->setNameFilterDisables(false);

    ui->treeViewCsvFiles->setModel(m_fileModel);
    ui->treeViewCsvFiles->setAnimated(false);
    ui->treeViewCsvFiles->setIndentation(20);
    ui->treeViewCsvFiles->setSortingEnabled(true);

    m_fileModelInventory->setNameFilters(QStringList() << "*.csv" << "*.CSV");
    m_fileModelInventory->setNameFilterDisables(false);

    ui->treeViewInventoryCsvFiles->setModel(m_fileModelInventory);
    ui->treeViewInventoryCsvFiles->setAnimated(false);
    ui->treeViewInventoryCsvFiles->setIndentation(20);
    ui->treeViewInventoryCsvFiles->setSortingEnabled(true);

    // Load saved folders
    QSettings settings;
    m_currentDir = settings.value("purchases/lastFolder").toString();

    if (!m_currentDir.isEmpty() && QDir(m_currentDir).exists()) {
        onFolderChanged(m_currentDir);
    } else {
        m_currentDir.clear();
        ui->treeViewCsvFiles->setModel(nullptr);
    }

    m_inventoryDir = settings.value("purchases/lastInventoryFolder").toString();

    if (!m_inventoryDir.isEmpty() && QDir(m_inventoryDir).exists()) {
        onInventoryFolderChanged(m_inventoryDir);
    } else {
        m_inventoryDir.clear();
        ui->treeViewInventoryCsvFiles->setModel(nullptr);
    }

    QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

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
    s_widgetPurchasesInstances.removeAll(this);
    delete ui;
}

bool WidgetPurchases::isInitialized() const
{
    return !ui->lineEditInventoryFolder->text().isEmpty()
           && !ui->lineEditPurchaseFolder->text().isEmpty();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void WidgetPurchases::_connectSlots()
{
    connect(ui->buttonSelectFolder, &QPushButton::clicked, this, &WidgetPurchases::selectFolder);
    connect(ui->buttonSelectInventoryFolder, &QPushButton::clicked, this, &WidgetPurchases::selectInventoryFolder);
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

    connect(ui->lineEditPurchaseFolder, &QLineEdit::textChanged, this, [this](const QString &path) {
        QSettings settings;
        settings.setValue("purchases/lastFolder", path);
        for (auto *instance : s_widgetPurchasesInstances) {
            instance->onFolderChanged(path);
        }
    });

    connect(ui->lineEditInventoryFolder, &QLineEdit::textChanged, this, [this](const QString &path) {
        QSettings settings;
        settings.setValue("purchases/lastInventoryFolder", path);
        for (auto *instance : s_widgetPurchasesInstances) {
            instance->onInventoryFolderChanged(path);
        }
    });
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

void WidgetPurchases::selectFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Purchase Folder"),
                                                    m_currentDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        ui->lineEditPurchaseFolder->setText(dir);
    }
}

void WidgetPurchases::selectInventoryFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Inventory Folder"),
                                                    m_inventoryDir,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        ui->lineEditInventoryFolder->setText(dir);
    }
}

void WidgetPurchases::onFolderChanged(const QString &path)
{
    m_currentDir = path;
    if (ui->lineEditPurchaseFolder->text() != m_currentDir) {
        const QSignalBlocker blocker(ui->lineEditPurchaseFolder);
        ui->lineEditPurchaseFolder->setText(m_currentDir);
    }

    ui->treeViewCsvFiles->setModel(m_fileModel);
    m_fileModel->setRootPath(m_currentDir);
    ui->treeViewCsvFiles->setRootIndex(m_fileModel->index(m_currentDir));
}

void WidgetPurchases::onInventoryFolderChanged(const QString &path)
{
    m_inventoryDir = path;
    if (ui->lineEditInventoryFolder->text() != m_inventoryDir) {
        const QSignalBlocker blocker(ui->lineEditInventoryFolder);
        ui->lineEditInventoryFolder->setText(m_inventoryDir);
    }

    ui->treeViewInventoryCsvFiles->setModel(m_fileModelInventory);
    m_fileModelInventory->setRootPath(m_inventoryDir);
    ui->treeViewInventoryCsvFiles->setRootIndex(m_fileModelInventory->index(m_inventoryDir));
}

void WidgetPurchases::editColumns()
{
    DialogEditCsvPurchases dialog(
                WorkingDirectoryManager::instance()->workingDir(), this);
    dialog.exec();
}

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

QStringList WidgetPurchases::getCsvFilePathsInventory(int year) const
{
    if (m_inventoryDir.isEmpty()) return QStringList();

    QStringList absolutePaths;
    QDirIterator it(m_inventoryDir, QStringList() << "*.csv" << "*.CSV", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        absolutePaths << it.next();
    }

    if (year > 0) {
        const QString yearPrefix = QString::number(year) + "-";
        absolutePaths.erase(
            std::remove_if(absolutePaths.begin(), absolutePaths.end(),
                [&yearPrefix](const QString &path) {
                    return !QFileInfo(path).fileName().startsWith(yearPrefix);
                }),
            absolutePaths.end());
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
