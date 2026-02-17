
# Qt / C++ Rules

## Exception Handling
- **DO NOT SILENCE CRITICAL EXCEPTIONS**: Especially in financial calculations (e.g. `CurrencyRateManager::convert`). 
  - **BAD**: Catching an exception, logging a warning, and returning `0.0` or a default value. This leads to corrupted data and silent failures.
  - **GOOD**: Let the exception propagate so the operation fails explicitly, or handle it by aborting the process/notifying the user immediately. 
  - Example of **FORBIDDEN CODE**:
    ```cpp
    try {
        return m_currencyRateManager->convert(val, currency, target, date);
    } catch (const ExceptionRateCurrency &e) {
        qWarning() << "Currency conversion failed:" << e.what();
        return 0.0; // NEVER DO THIS for financial data
    }
    ```

## General Qt
- Use `QString` and `QList` over `std::string` and `std::vector` when interacting with Qt APIs.
- Prefer `qDebug()` for logging.

## Memory Management
- Use `QObject` parent-child hierarchy for automatic memory management.
- Use `QSharedPointer` or `QScopedPointer` for check-ins that are not QObjects.

## Test Integrity
- **DO NOT SUPPRESS EXCEPTIONS TO PASS TESTS**: If a test fails due to missing data (e.g. missing FBA center in `FbaCentersTable`), **DO NOT** catch the exception in the production code to make the test pass.
- **FIX THE ROOT CAUSE**: Instead, update the test data or the data loading logic (e.g. `_fillIfEmpty`) to include the missing data. The test failure is a valid signal that data is incomplete.
