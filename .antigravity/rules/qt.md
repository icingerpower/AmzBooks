# Qt Best Practices

- **QString::arg**: Always use the multi-argument overload `QString("%1 %2").arg(a, b)` instead of chained calls `QString("%1 %2").arg(a).arg(b)` to avoid multiple allocations and potential issues.

## Exception Handling
- **Inheritance**: Exceptions should ideally inherit from `QException`.
- **Cross-Thread Safety**: They must implement `raise()` and `clone()` methods to be compatible with Qt's cross-thread exception transport.
- **Detail**: Always provide a title and detailed error text.
- **Catching**: Do not catch `const std::exception &e` if the exception inherits from `QException`. Catch `const QException &e` (or the specific type) first.
- **UI Reporting**: Exceptions caught in UI models or logic **MUST** be reported to the user via specific signals (e.g. `exceptionOccurred`) connected to UI elements (like `QMessageBox`), and **NOT** hidden via `qWarning` or `qDebug`.

- **CsvHeaderException**: use `columnValuesError()` to retrieve the list of missing columns instead of using `what()`.
- **ExceptionFileError**: use `errorTitle()` and `errorText()` to display the error message.

## QAbstractTableModel Implementations
- **Robust Loading**:
    - Loading logic **MUST** be robust to schema changes. It should handle added, deleted, or re-ordered columns gracefully.
    - **Header Mapping**: Always read the header line to map column names to indices dynamically. Do not rely on fixed column indices.
- **Technical IDs**: Use hidden, stable technical IDs ("Hidden ID") for logic, which are stored in the file but not displayed in `columnCount`/`data`.
- **Localization**:
    - Use `tr()` for all user-facing strings (headers, parameter names).
    - **Exception**: Do **NOT** use `tr()` for matching hidden internal IDs, folder names, or technical keys.

## C++ Header/Source Separation
- **Implementation**: Header files (`.h`) should contain only declarations. Implementations must be in source files (`.cpp`).
    - **Exceptions**: Even simple Exception classes must follow this (constructors, verify/clone methods, etc.).
    - **Exception**: Template classes or inline performance-critical methods (use judgment) may remain in headers.

## Coding Style
- **If Statements**:
    - Never write `if` instructions on the same row.
    - **Brackets**: Always use brackets `{}` for `if`/`else` blocks, even for single instructions.

## API & Importer Configuration
- **Parameter Structure**: functionality that requires list configuration (e.g. list of shops, list of accounts) **MUST** use parallel arrays (e.g. `shopNames`, `appIds`) instead of a single parameter containing a list of complex JSON objects.
    - **Reasoning**: This simplifies validation and UI generation.
- **Validation**: Validators for these parameters **MUST** strictly check that the input is a valid JSON array and that all elements are of the expected type (e.g. strings).
