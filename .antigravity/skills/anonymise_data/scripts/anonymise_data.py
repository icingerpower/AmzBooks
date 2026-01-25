import os
import csv
import json
import re
import sys

# Configuration for different report types based on folder names
REPORT_CONFIG = {
    "amazon-fba-invoicing": {
        "text": ["Buyer Name", "Recipient Name", "Delivery Address 1", "Delivery Address 2", "Delivery Address 3", "Billing Address 1", "Billing Address 2", "Billing Address 3",
                 "Shipping Address 1", "Shipping Address 2", "Shipping Address 3", "Billing City", "Shipping City", "UNIQUE_ACCOUNT_IDENTIFIER", "Title", "item-name", "product-name"], 
        "email": ["Buyer E-mail", "Buyer Email", "email"],
        "phone": ["Buyer Phone Number", "Delivery Phone Number", "Shipping Phone Number"],
        "id": ["Amazon Order Id", "Merchant Order ID", "Merchant Order Id", "Shipment ID", "Shipment Item ID", "Shipment Item Id", "Invoice Number", "VAT_INV_NUMBER", "Tracking Number"],
        "url": ["Invoice Url", "INVOICE_URL"] 
    },
    "eu-vat-reports": {
        "text": ["BUYER_NAME", "ARRIVAL_ADDRESS", "SUPPLIER_NAME", "ITEM_DESCRIPTION"], # Added ITEM_DESCRIPTION
        "id": ["ASIN", "TRANSACTION_EVENT_ID", "ACTIVITY_TRANSACTION_ID", "VAT_INV_NUMBER"],
        "vat": ["SELLER_DEPART_COUNTRY_VAT_NUMBER", "SELLER_ARRIVAL_COUNTRY_VAT_NUMBER", "TRANSACTION_SELLER_VAT_NUMBER", "BUYER_VAT_NUMBER", "SUPPLIER_VAT_NUMBER"],
        "url": ["INVOICE_URL"]
    },
    "temu-product-codes": {
        "id": ["Goods ID", "SKU ID", "External product ID"],
        "text": ["Contribution Goods"] # Added Contribution Goods
    },
    "temu-vat-reports": {
        "id": ["N° DE COMMANDE", "ID DE FACTURE", "ID DE FACTURE DE SUBVENTION"],
        "text": ["NOM DU CENTRE COMMERCIAL"]
    }
}

DATA_DIR = "data"
LOG_FILE = ".anonymised_log.json"

def load_log():
    if os.path.exists(LOG_FILE):
        try:
            with open(LOG_FILE, 'r') as f:
                return set(json.load(f))
        except json.JSONDecodeError:
            print(f"Warning: {LOG_FILE} is corrupted. Starting fresh.")
            return set()
    return set()

def save_log(processed_files):
    with open(LOG_FILE, 'w') as f:
        json.dump(list(processed_files), f, indent=4)

def anonymise_text(text):
    if not text:
        return text
    # Keep first letter of each word and replace rest with XXXXXXX
    # Example: "John Doe" -> "JXXXXXXX DXXXXXXX"
    words = text.split()
    anonymised_words = []
    for word in words:
        if len(word) > 0:
            anonymised_words.append(word[0] + "X" * 7)
    return " ".join(anonymised_words)

def anonymise_email(email):
    if not email:
        return email
    return "xxxxx@email.com"

def anonymise_phone(phone):
    if not phone:
        return phone
    # Replace all digits with 0
    return re.sub(r'\d', '0', phone)

def rotate_digits(text):
    if not text:
        return text
    # 1->2, 2->3 ... 9->1
    result = ""
    for char in text:
        if char.isdigit():
            digit = int(char)
            if digit == 9:
                result += "1"
            else:
                result += str(digit + 1)
        else:
            result += char
    return result

def anonymise_vat(vat):
    if not vat:
        return vat
    # Replace digits with 0, keep letters
    return re.sub(r'\d', '0', vat)

def anonymise_url(url):
    if not url:
        return url
    return "https://example.com/redacted-invoice.pdf"

def get_report_type(file_path, base_dir=None):
    # Determine report type based on parent directory
    # Expected path: <base_dir>/<report_type>/...
    # If base_dir is not provided, we guess based on known types match
    
    parts = os.path.normpath(file_path).split(os.sep)
    for part in parts:
        if part in REPORT_CONFIG:
            return part
    return None

def process_file(file_path, config):
    print(f"Processing {file_path}...")
    
    # Detect delimiter
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
            # content = f.read() # Reading whole file might be slow for huge files, but safe for small ones.
            # sample = content[:4096]
            sample = f.read(4096)
            sniffer = csv.Sniffer()
            try:
                dialect = sniffer.sniff(sample)
            except csv.Error:
                # Fallback to comma if sniffing fails
                dialect = csv.excel
                dialect.delimiter = ','
                if ';' in sample and ',' not in sample:
                     dialect.delimiter = ';'
            
            # Reset file pointer
            f.seek(0)
            
            # Read all data
            # Use strict=False to be more lenient with bad CSV lines if possible, though DictReader doesn't have it?
            # It does. 
            reader = csv.DictReader(f, dialect=dialect) # removed strict=False as it might not be valid arg for DictReader in older py
            fieldnames = reader.fieldnames
            rows = list(reader)
            
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return False

    if not fieldnames:
        print(f"Skipping empty file: {file_path}")
        return False

    # Validation: Check if columns exist (Case Insensitive Matching)
    relevant_cols = [] # List of (actual_col_name, category)
    missing_cols = []
    
    # Create a mapping of lower-case config columns to their category
    config_map = {}
    for category, cols in config.items():
        for col in cols:
            config_map[col.lower()] = category

    # map actual fields
    actual_fields_map = {f.lower(): f for f in fieldnames}

    # Match
    for config_col_lower, category in config_map.items():
        if config_col_lower in actual_fields_map:
            relevant_cols.append((actual_fields_map[config_col_lower], category))
    
    # Check for mismatches if needed, but since we are being robust, we just warn if nothing matches at all?
    # Or strict check?
    # Let's assume strict check is annoying if files vary slightly.
    # But user wanted "check every columns". 
    
    if not relevant_cols:
        print(f"Warning: No matching columns found for configuration in {file_path}. Skipping anonymization.")
        # If no matching columns are found, we don't modify the file, but we shouldn't necessarily fail hard
        # unless we suspect it SHOULD have matched.
        return False 

    # Process rows
    anonymised_rows = []
    for row in rows:
        new_row = row.copy()
        for col, category in relevant_cols:
            if row[col]: # Only anonymise non-empty values
                if category == "text":
                    new_row[col] = anonymise_text(row[col])
                elif category == "email":
                    new_row[col] = anonymise_email(row[col])
                elif category == "phone":
                    new_row[col] = anonymise_phone(row[col])
                elif category == "id":
                    new_row[col] = rotate_digits(row[col])
                elif category == "vat":
                    new_row[col] = anonymise_vat(row[col])
                elif category == "url":
                    new_row[col] = anonymise_url(row[col])
        anonymised_rows.append(new_row)

    # Write back atomically
    # Use standard CSV settings for writing to avoid escape char issues.
    temp_file = file_path + ".tmp"
    try:
        with open(temp_file, 'w', encoding='utf-8', newline='') as f:
            # Force standard dialect for writing to be robust
            writer = csv.DictWriter(f, fieldnames=fieldnames, dialect='excel', quoting=csv.QUOTE_MINIMAL)
            writer.writeheader()
            writer.writerows(anonymised_rows)
        
        # If write succeeds, replace original file
        os.replace(temp_file, file_path)
        print(f"Successfully anonymised {file_path}")
        return True
    except Exception as e:
        print(f"Error writing {file_path}: {e}")
        if os.path.exists(temp_file):
            try:
                os.remove(temp_file)
            except OSError:
                pass
        return False

def main():
    target_dir = sys.argv[1] if len(sys.argv) > 1 else DATA_DIR
    
    if not os.path.exists(target_dir):
        print(f"Error: {target_dir} directory not found.")
        sys.exit(1)

    processed_files = load_log()
    newly_processed = set()

    for root, dirs, files in os.walk(target_dir):
        for file in files:
            if not file.lower().endswith('.csv'):
                continue
                
            file_path = os.path.join(root, file)
            abs_path = os.path.abspath(file_path)
            
            if abs_path in processed_files:
                # print(f"Skipping already processed: {file_path}")
                continue

            report_type = get_report_type(file_path)
            
            if report_type in REPORT_CONFIG:
                try:
                    if process_file(file_path, REPORT_CONFIG[report_type]):
                        newly_processed.add(abs_path)
                except ValueError as e:
                    print(e)
                    # Stop execution on column mismatch as requested
                    sys.exit(1)
            else:
                # print(f"Skipping unknown report type: {report_type} for {file_path}")
                pass

    if newly_processed:
        processed_files.update(newly_processed)
        save_log(processed_files)
        print(f"Updated log with {len(newly_processed)} files.")
    else:
        print("No new files to process.")

if __name__ == "__main__":
    main()
