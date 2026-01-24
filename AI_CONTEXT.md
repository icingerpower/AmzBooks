# AI Context & Developer Notes

This file contains instructions for compiling and running unit tests for the AmzBooks project, as well as project-specific context to assist future AI in development tasks.

## Build & Test Instructions

### Compilation
The project uses CMake. To compile the unit tests, use the following command from the project root:

```bash
cmake --build build --target AmzBooksAutoTests
```

### Running Unit Tests
After successful compilation, the test executable is located in the `build/AmzBooksAutoTests` directory. The executable name is `TestOrders` (not `AmzBooksAutoTests`).

To run the tests:

```bash
./build/AmzBooksAutoTests/TestOrders
```

## Project Structure & Learnings

### Core Models
- **Exception Handling**:
    - Exceptions should ideally inherit from `QException`.
    - They must implement `raise()` and `clone()` methods to be compatible with Qt's cross-thread exception transport.
    - Example: `TaxSchemeInvalidException` or `CompanyInfoException`.
    - Always provide a title and detailed error text.

- **Activity**: Represents a normalized accounting posting line. Key fields include:
    - `m_saleType`: Enum `SaleType::Products` or `SaleType::Service` (Added Jan 2026).
    - `m_countryCodeVatPaidTo`: ISO 3166-1 alpha-2 code indicating where VAT is paid (Added Jan 2026).
    - `m_vatTerritoryTo`: Special VAT territory destination.
    - **Refactor Note (Jan 2026)**: 
        - `Shipment` is now a lightweight wrapper around `Activity`. It **does not** hold line items.
        - `InvoicingInfo` holds the line items and performs tax rounding adjustments to match the Activity totals upon construction or `setItems`.

- **TaxResolver**: deterministic engine for resolving tax contexts.
    - Uses `VatTerritoryResolver` for looking up special territories (e.g., Canary Islands, Livigno).

### Testing
- Tests are written using `QtTest`.
- **TestOrders**: Main test file `AmzBooksAutoTests/tst_testorders.cpp`.
- **TestTaxResolver**: Separate test executable for tax logic validation (`AmzBooksAutoTests/tst_test_tax_resolver.cpp`).
    - Validates against synthetic scenarios and real-world Amazon VAT reports (`data/eu-vat-reports`).
    - Uses `validation_blacklist.txt` to skip known edge case transactions.
- When updating `Activity::create` calls in tests, ensure all parameters are provided. For `countryCodeVatPaidTo`, avoid using empty strings if possible; prefer valid country codes (e.g., "DE", "FR") to keep tests realistic and self-documenting.

### Common Pitfalls
- **Executable Names**: 
    - `AmzBooksAutoTests` CMake target builds multiple test executables.
    - Run `./build/AmzBooksAutoTests/TestOrders` for general order logic.
    - Run `./build/AmzBooksAutoTests/TestTaxResolver` for tax validation.
- **TaxResolver Logic**: `TaxResolver` assumes EU OSS (Union Scheme) declares tax in the **Destination** country (`taxDeclaringCountryCode` = `countryCodeTo`), not Origin.
- **VAT Anomalies**:
    - **LU 2023**: Amazon sometimes applied 17% (2022 rate) in early 2023. This is resolved by preferring `TAX_CALCULATION_DATE` (if available) over Transaction Date in VAT reports. The system expects 16% for 2023 and 17% for 2024.
    - **IT Rate**: 22% rate logic in `VatResolver` was updated to cover pre-2021 dates (fallback).
