name: anonymise_data
description: Anonymise sensitive data in CSV reports (Amazon, Temu, VAT reports).
instructions: |
  This skill provides a Python script to anonymize sensitive information in CSV report files.
  
  It handles various report types including:
  - Amazon FBA Invoicing (EU & US)
  - EU VAT Reports
  - Temu Product Codes
  - Temu VAT Reports
  
  The script performs the following anonymization techniques:
  - **Text (Names/Addresses)**: First letter retained, rest replaced with 'X'.
  - **Emails**: Replaced with dummy value.
  - **Phone Numbers**: Digits replaced with '0'.
  - **IDs**: Digits rotated (1->2, etc) to preserve uniqueness but break linkage.
  - **VAT Numbers**: Digits replaced with '0'.
  
  **Usage**:
  To run the anonymization:
  ```bash
  python3 scripts/anonymise_data.py <target_directory>
  ```
  Example:
  ```bash
  python3 scripts/anonymise_data.py /path/to/data
  ```
  
  **Idempotency**:
  The script maintains a `.anonymised_log.json` file in the working directory to track processed files. It will not process the same file twice unless the log is cleared.
