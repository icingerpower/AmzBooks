# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AmzBooks is a Qt 6 / C++20 desktop accounting application for Amazon and e-commerce sellers. It imports orders from multiple marketplaces (Amazon EU/US, Temu, CommerceHQ), resolves VAT, and generates bookkeeping entries.

**Tech stack**: Qt 6.8, C++20, CMake, SQLite (via Qt SQL), QCoro (coroutines), QtTest.

## Build Commands

```bash
# Configure (once)
cd /home/cedric/Applications/AmzBooks/build && cmake ..

# Build the library only
cmake --build . --target AmzBooksLib -j4

# Build specific test targets
cmake --build . --target TestOrders TestTaxResolver -j4

# Build all tests
cmake --build . --target AmzBooksAutoTests

# Run a specific test executable
./AmzBooksAutoTests/TestOrders
./AmzBooksAutoTests/TestTaxResolver
./AmzBooksAutoTests/TestBookEntries
./AmzBooksAutoTests/TestAmazonPaymentReports
# etc. — see AmzBooksAutoTests/CMakeLists.txt for all target names
```

To run a single test method: `./AmzBooksAutoTests/TestOrders testMethodName`

## Repository Structure

```
AmzBooksLib/          Core library (no Qt Widgets dependency)
  books/              Bookkeeping models, tax engine, journal
  orders/             Order importers, OrderManager (SQLite), data models
  banks/              Bank statement importers
  inventory/          Inventory move tracking
  profit/             Profit calculation
AmzBooks/             Qt Widgets GUI application
  gui/panes/          Tab/pane widgets (one per feature area)
  gui/dialogs/        Dialogs
  gui/delegates/      QStyledItemDelegate subclasses for table editing
AmzBooksAutoTests/    QtTest test suite — one executable per test class
data/                 Test fixtures (amazon-vat-reports/, amazon-transactions/, etc.)
```

New source files must be registered in the relevant `.cmake` file (`books/books.cmake` or `orders/orders.cmake`).

## Architecture

### Order pipeline

1. **AbstractImporter** (file or API subclass) parses raw data into `OrderInfos` (Shipments, Refunds, InvoicingInfo, Addresses).
2. **OrderManager** (SQLite-backed) stores shipments/refunds with Draft/Published versioning. Re-importing conflicting data generates reversal + new postings atomically. Entry point for `recordShipmentFromSource()`, `publish()`.
3. **TaxResolver** — pure, stateless engine: given `(dateTime, countryFrom, countryTo, saleType, isBusiness, vatTerritories)` returns a `TaxContext` (scheme, declaring country, jurisdiction level). Uses `VatTerritoryResolver` for special territories (Canary Islands, Livigno, etc.).
4. **BookSaverFull** / **AbstractBooksTable** — converts published shipments into double-entry bookkeeping rows via `BooksConnections`.

### Key model classes

- **Activity** — normalised posting line. Fields: `m_saleType` (Products/Service), `m_countryCodeVatPaidTo`, `m_vatTerritoryTo`. `Shipment` is a lightweight wrapper around `Activity`; it does NOT hold line items.
- **InvoicingInfo** — holds line items; adjusts tax rounding to match Activity totals on construction/`setItems`.
- **AbstractBooksTable** — `QAbstractTableModel` base with 6 standard columns: Date, Amount, Currency, Label, Acct1, Acct2. Subclasses implement `getId()` and `load(int year)`.
- **BooksConnections** — persists linkage between bank/purchase entries and journal entries (CSV file).
- **CompanyInfosTable** — single source of truth for company currency, country code, VAT numbers. **Never hardcode "EUR" or "FR"** — always read from `CompanyInfosTable`.

### Importers

Two abstract bases:
- **AbstractImporterFile** — file-based (CSV/TSV drop). Subclasses: `ImporterFileAmazonVatEu`, `ImporterFileAmazonTransactions`, `ImporterFileTemuOrders`, `ImporterFileCommerceHQ`, etc.
- **AbstractImporterApi** — API-based with OAuth/token params. Subclasses: `ImporterApiAmazonEu`, `ImporterApiAmazonAmerica`, `ImporterApiTemu`, `ImporterApiCommerceHQ`.

Each importer declares its parameters via `getRequiredParams()` (key, label, validator) — the GUI builds the settings form from this.

### Callback pattern for missing data

When importing, missing config (FBA centers, VAT rates, accounts) is handled with a `callbackAddIfMissing`. Always loop:

```cpp
while (true) {
    auto result = lookup(id);
    if (result) co_return result;
    if (!callbackAddIfMissing) break;
    bool retry = co_await callbackAddIfMissing(title, text);
    result = lookup(id);
    if (result) co_return result;
    if (!retry) throw ...;
}
```

## Coding Rules

### Braces are mandatory
- Always add braces, even for single-line `if`/`for`/`while`/`else` bodies:
  ```cpp
  // WRONG
  if (condition) doSomething();

  // CORRECT
  if (condition) {
      doSomething();
  }
  ```

### Use `const auto &` to avoid copies (non-coroutine code)
- Outside coroutines, store temporary values in `const auto &` instead of `auto` to avoid unnecessary copies.
- **Never chain method calls** on a temporary when capturing by reference — break the chain into separate variables to keep each intermediate alive:
  ```cpp
  // WRONG — dangling reference, ret2() operates on a temporary
  const auto &result = var.ret1().ret2();

  // CORRECT — each intermediate is kept alive
  const auto &temp1 = var.ret1();
  const auto &result = temp1.ret2();
  ```

### `QString::arg` — use multi-argument overload
- When `QString::arg` is called with more than one placeholder, use the multi-argument overload for correctness and performance:
  ```cpp
  // WRONG — chained .arg() calls (ambiguous with numbered placeholders)
  QString s = QString("%1 owes %2").arg(name).arg(amount);

  // CORRECT — single multi-argument call
  QString s = QString("%1 owes %2").arg(name, amount);
  ```

### Exception handling
- Use `ExceptionWithTitleText(title, text)` and call `.raise()` — **not** `throw`. Always as a two-line pattern:
  ```cpp
  ExceptionWithTitleText ex("Invalid Inventory Move",
                            "transactionId cannot be empty");
  ex.raise();
  ```
- **Never silence exceptions in financial calculations** (e.g. `CurrencyRateManager::convert`). Catching and returning a default value leads to corrupted data. The following pattern is **forbidden**:
  ```cpp
  try {
      return m_currencyRateManager->convert(val, currency, target, date);
  } catch (const ExceptionRateCurrency &e) {
      qWarning() << "Currency conversion failed:" << e.what();
      return 0.0; // NEVER DO THIS for financial data
  }
  ```
  Let the exception propagate so the operation fails explicitly, or abort/notify the user immediately.

### Use `tr()` / `QObject::tr()` for user-visible text
- Any text displayed to the user (labels, messages, tooltips, column headers) must be wrapped in `tr()` or `QObject::tr()`.
- Private/hidden IDs, log messages, and internal keys are exempt.

### Implementations in `.cpp`, not `.h`
- Method implementations must go in the `.cpp` file whenever possible — **not** inline in the header.
- Exceptions: trivial getters/setters, `constexpr` functions, and template definitions that must remain in headers.

### Header files: document non-obvious logic
- `.h` files should contain concise comments on details that are **hard to guess** or have caused (or could cause) bugs. Focus on invariants, ownership semantics, valid value ranges, and non-obvious relationships between fields.

### Forward-declare in `.h`, include in `.cpp`
- In header files, prefer forward declarations (`class Foo;`) over `#include` whenever possible.
- Place the actual `#include` in the `.cpp` file. This improves compilation performance.

### Compile cleanly — fix all warnings
- Code must compile with **zero warnings** (treat yellow warnings as errors).
- In particular, add `std::as_const()` in range-for loops over non-const containers to avoid detaching implicitly-shared Qt containers:
  ```cpp
  for (const auto &item : std::as_const(m_items)) { ... }
  ```

### Use `std` algorithms with Qt containers
- When compatible, prefer `std::` algorithms (`std::find_if`, `std::any_of`, `std::transform`, `std::sort`, etc.) over hand-written loops on Qt containers for better performance and readability.

### Test integrity
- **Never suppress exceptions to make tests pass.** If a test fails due to missing data (e.g. a missing FBA center in `FbaCentersTable`), fix the root cause — update the test data or the data-loading logic (e.g. `_fillIfEmpty`). The test failure is a valid signal that data is incomplete.

### Qt conventions
- Use `QString` / `QList` over `std::string` / `std::vector` when interfacing with Qt.
- Prefer `qDebug()` for logging.
- Use `QObject` parent-child for memory management; `QSharedPointer`/`QScopedPointer` for non-QObjects.
- Qt test strings: use `%1`, `%2` with `.arg()` — NOT `printf`-style `%.2f`.

### UI — constrained fields
- Country → `QComboBox` with standard list (US, CA, CN, FR, DE, then EU countries)
- Currency → `QComboBox` (EUR, USD, GBP, CHF, CAD, AUD, JPY, SEK, NOK, DKK, PLN, CZK, HUF)
- Payment type → `QComboBox` (Instant / After X Days / End of Next Month)
- Boolean flags → `QCheckBox`, never a text field
- Enforce via `QStyledItemDelegate` subclass in editable `QTableView`s (see `DialogEditServiceClients.cpp` → `ServiceClientDelegate` as pattern).

### Test patterns
- `QFAIL` expands to `void` return — use `QTest::qFail(..., __FILE__, __LINE__); return {};` in non-void helpers.
- Exception test: `bool threw = false; try{...} catch(ExceptionWithTitleText&){threw=true;} QVERIFY(threw);`
- `QTemporaryDir` for temporary file operations.

## Known Quirks

- **TaxResolver / EU OSS**: `taxDeclaringCountryCode` equals `countryCodeTo` (destination) for Union Scheme — not origin.
- **LU VAT 2023**: Amazon used 17% (2022 rate) in early 2023. System expects 16% for 2023, 17% for 2024. Prefer `TAX_CALCULATION_DATE` over transaction date when available.
- **Amazon payment filenames**: amounts contain dots (e.g. `177.90USD`). Never use `completeBaseName()` — use `fileName()` and strip only purely-alphabetic extensions.
- **`-Wl,--whole-archive`**: Some test targets link with `--whole-archive` to force static initializers (self-registering recorders). Required for `TestRecorders`, `TestBookAccounts`, `TestBookAccountSelfVatTable`.
