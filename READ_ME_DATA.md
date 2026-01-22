# Test Data (Amazon VAT CSV Reports)

To run the unit tests successfully, you must provide Amazon VAT CSV reports locally.

## Location

Create a `data/` folder at the repository root and place the Amazon VAT CSV exports inside:

repo-root/
data/
<amazon-vat-report>.csv
...


## Privacy / Git

These CSV files may contain sensitive business information and are therefore excluded from Git (not committed).

## Notes

- Use the original Amazon-exported CSV files (keep headers/structure unchanged).
- If tests report missing files, verify the `data/` folder exists at the repository root and contains the expected CSV reports.

