#ifndef OWL_PII_SCRUBBER_H_
#define OWL_PII_SCRUBBER_H_

#include <string>
#include <vector>
#include <regex>
#include <map>

namespace OwlPII {

/**
 * PII Category enumeration for tracking what types of PII were found
 */
enum class PIICategory {
  EMAIL,
  PHONE,
  SSN,
  CREDIT_CARD,
  IP_ADDRESS,
  STREET_ADDRESS,
  DATE_OF_BIRTH,
  MEDICAL_RECORD_NUMBER,
  API_KEY,
  SENSITIVE_URL,
  ACCOUNT_NUMBER,
  ZIP_CODE,
  DRIVER_LICENSE,
  PASSPORT,
  BANK_ACCOUNT,
  // New categories for enhanced detection
  PERSON_NAME,
  ORGANIZATION_NAME,
  LOCATION_NAME,
  HEALTH_INSURANCE_NUMBER,
  VEHICLE_IDENTIFICATION_NUMBER,
  TAX_ID,
  IBAN,
  SWIFT_CODE,
  CRYPTO_ADDRESS,
  MAC_ADDRESS,
  USERNAME,
  FILE_PATH,
  NATIONAL_ID,
  BIOMETRIC_ID
};

/**
 * Statistics about PII scrubbing
 */
struct ScrubStats {
  int total_items_found = 0;
  std::map<PIICategory, int> by_category;

  void AddDetection(PIICategory category) {
    total_items_found++;
    by_category[category]++;
  }

  std::string ToString() const;
};

/**
 * OwlPIIScrubber - Scrubs PII and HIPAA-protected data from text
 *
 * This class provides comprehensive PII detection and redaction to prevent
 * sensitive information from being sent to third-party LLM APIs.
 *
 * IMPORTANT: This scrubber is ONLY used when sending data to third-party APIs.
 * Built-in LLMs (on-device) do NOT require scrubbing as data stays local.
 *
 * PII Categories Detected:
 * - Email addresses (with validation and context awareness)
 * - Phone numbers (US and international formats with validation)
 * - Social Security Numbers (SSN with validation)
 * - Credit card numbers (Visa, MC, Amex, Discover with Luhn validation)
 * - IP addresses (IPv4 and IPv6 with range validation)
 * - Street addresses (US and international formats)
 * - Dates of birth (with age validation)
 * - Medical record numbers
 * - API keys and tokens (various formats)
 * - Sensitive URL parameters
 * - Account numbers
 * - ZIP codes (with context awareness)
 * - Driver's licenses (US state formats)
 * - Passport numbers (international formats)
 * - Bank account numbers (US routing + account)
 * - Person names (NER-based detection)
 * - Organization names
 * - Location names (cities, countries, addresses)
 * - Health insurance numbers (Medicare, Medicaid, private)
 * - Vehicle Identification Numbers (VIN with validation)
 * - Tax IDs (EIN, TIN, ITIN)
 * - IBAN (International Bank Account Numbers)
 * - SWIFT/BIC codes
 * - Cryptocurrency addresses (Bitcoin, Ethereum)
 * - MAC addresses
 * - Usernames and handles
 * - File paths with usernames
 * - National IDs (various countries)
 * - Biometric identifiers
 *
 * Usage:
 *   OwlPIIScrubber scrubber;
 *   std::string clean_text = scrubber.ScrubText(user_input);
 *   ScrubStats stats = scrubber.GetStats();
 */
class OwlPIIScrubber {
 public:
  OwlPIIScrubber();
  ~OwlPIIScrubber() = default;

  /**
   * Scrub PII from text, replacing with placeholders like [EMAIL], [PHONE], etc.
   *
   * @param text Input text that may contain PII
   * @return Scrubbed text with PII replaced by category placeholders
   */
  std::string ScrubText(const std::string& text);

  /**
   * Get statistics about what PII was detected and scrubbed
   *
   * @return ScrubStats object with counts by category
   */
  ScrubStats GetStats() const { return stats_; }

  /**
   * Reset statistics
   */
  void ResetStats() { stats_ = ScrubStats(); }

  /**
   * Enable/disable specific PII categories
   *
   * @param category The PII category to enable/disable
   * @param enabled True to enable detection, false to disable
   */
  void SetCategoryEnabled(PIICategory category, bool enabled);

  /**
   * Check if a specific category is enabled
   *
   * @param category The PII category to check
   * @return True if enabled, false otherwise
   */
  bool IsCategoryEnabled(PIICategory category) const;

 private:
  /**
   * Initialize all regex patterns for PII detection
   */
  void InitializePatterns();

  /**
   * Scrub emails from text
   */
  std::string ScrubEmails(const std::string& text);

  /**
   * Scrub phone numbers from text (US and international)
   */
  std::string ScrubPhoneNumbers(const std::string& text);

  /**
   * Scrub SSN from text (###-##-####)
   */
  std::string ScrubSSN(const std::string& text);

  /**
   * Scrub credit card numbers from text
   */
  std::string ScrubCreditCards(const std::string& text);

  /**
   * Scrub IP addresses (IPv4 and IPv6)
   */
  std::string ScrubIPAddresses(const std::string& text);

  /**
   * Scrub street addresses (US format)
   */
  std::string ScrubStreetAddresses(const std::string& text);

  /**
   * Scrub dates of birth (various formats)
   */
  std::string ScrubDatesOfBirth(const std::string& text);

  /**
   * Scrub medical record numbers
   */
  std::string ScrubMedicalRecords(const std::string& text);

  /**
   * Scrub API keys and tokens
   */
  std::string ScrubAPIKeys(const std::string& text);

  /**
   * Scrub sensitive URL parameters (tokens, keys, passwords)
   */
  std::string ScrubSensitiveURLs(const std::string& text);

  /**
   * Scrub account numbers
   */
  std::string ScrubAccountNumbers(const std::string& text);

  /**
   * Scrub ZIP codes
   */
  std::string ScrubZipCodes(const std::string& text);

  /**
   * Scrub driver's licenses (US format)
   */
  std::string ScrubDriverLicenses(const std::string& text);

  /**
   * Scrub passport numbers
   */
  std::string ScrubPassports(const std::string& text);

  /**
   * Scrub bank account numbers
   */
  std::string ScrubBankAccounts(const std::string& text);

  /**
   * Scrub person names using NER patterns
   */
  std::string ScrubPersonNames(const std::string& text);

  /**
   * Scrub organization names
   */
  std::string ScrubOrganizationNames(const std::string& text);

  /**
   * Scrub location names
   */
  std::string ScrubLocationNames(const std::string& text);

  /**
   * Scrub health insurance numbers (Medicare, Medicaid, etc.)
   */
  std::string ScrubHealthInsurance(const std::string& text);

  /**
   * Scrub Vehicle Identification Numbers (VIN)
   */
  std::string ScrubVIN(const std::string& text);

  /**
   * Scrub Tax IDs (EIN, TIN, ITIN)
   */
  std::string ScrubTaxIDs(const std::string& text);

  /**
   * Scrub IBAN (International Bank Account Numbers)
   */
  std::string ScrubIBAN(const std::string& text);

  /**
   * Scrub SWIFT/BIC codes
   */
  std::string ScrubSWIFT(const std::string& text);

  /**
   * Scrub cryptocurrency addresses
   */
  std::string ScrubCryptoAddresses(const std::string& text);

  /**
   * Scrub MAC addresses
   */
  std::string ScrubMACAddresses(const std::string& text);

  /**
   * Scrub usernames and handles
   */
  std::string ScrubUsernames(const std::string& text);

  /**
   * Scrub file paths with usernames
   */
  std::string ScrubFilePaths(const std::string& text);

  /**
   * Scrub national IDs
   */
  std::string ScrubNationalIDs(const std::string& text);

  /**
   * Scrub biometric identifiers
   */
  std::string ScrubBiometricIDs(const std::string& text);

  /**
   * Validate credit card using Luhn algorithm
   */
  bool IsValidCreditCard(const std::string& number) const;

  /**
   * Validate SSN format
   */
  bool IsValidSSN(const std::string& ssn) const;

  /**
   * Validate VIN using checksum
   */
  bool IsValidVIN(const std::string& vin) const;

  /**
   * Validate IBAN using checksum
   */
  bool IsValidIBAN(const std::string& iban) const;

  /**
   * Validate IP address ranges
   */
  bool IsValidIPAddress(const std::string& ip) const;

  /**
   * Check if email is whitelisted (example.com, test.com, etc.)
   */
  bool IsWhitelistedEmail(const std::string& email) const;

  /**
   * Check if text contains name indicators (Mr., Mrs., Dr., etc.)
   */
  bool HasNameIndicators(const std::string& text) const;

  /**
   * Extract capitalized words that might be names
   */
  std::vector<std::string> ExtractPotentialNames(const std::string& text) const;

  // Regex patterns for PII detection
  std::regex email_pattern_;
  std::regex phone_pattern_;
  std::regex ssn_pattern_;
  std::regex credit_card_pattern_;
  std::regex ipv4_pattern_;
  std::regex ipv6_pattern_;
  std::regex street_address_pattern_;
  std::regex dob_pattern_;
  std::regex medical_record_pattern_;
  std::regex api_key_pattern_;
  std::regex sensitive_url_pattern_;
  std::regex account_number_pattern_;
  std::regex zip_code_pattern_;
  std::regex driver_license_pattern_;
  std::regex passport_pattern_;
  std::regex bank_account_pattern_;

  // New patterns for enhanced detection
  std::regex person_name_pattern_;
  std::regex organization_pattern_;
  std::regex location_pattern_;
  std::regex health_insurance_pattern_;
  std::regex vin_pattern_;
  std::regex ein_pattern_;
  std::regex iban_pattern_;
  std::regex swift_pattern_;
  std::regex bitcoin_pattern_;
  std::regex ethereum_pattern_;
  std::regex mac_address_pattern_;
  std::regex username_pattern_;
  std::regex file_path_pattern_;
  std::regex national_id_pattern_;
  std::regex biometric_pattern_;
  std::regex name_title_pattern_;
  std::regex medicare_pattern_;
  std::regex medicaid_pattern_;

  // Statistics
  ScrubStats stats_;

  // Category enable/disable flags
  std::map<PIICategory, bool> category_enabled_;

  // Whitelisted domains and patterns
  std::vector<std::string> whitelisted_email_domains_;
  std::vector<std::string> common_test_patterns_;
};

/**
 * Get category name as string
 */
std::string GetCategoryName(PIICategory category);

}  // namespace OwlPII

#endif  // OWL_PII_SCRUBBER_H_
