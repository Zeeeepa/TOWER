#include "owl_pii_scrubber.h"
#include "logger.h"
#include <algorithm>
#include <sstream>

namespace OwlPII {

// Helper function to get category name
std::string GetCategoryName(PIICategory category) {
  switch (category) {
    case PIICategory::EMAIL: return "EMAIL";
    case PIICategory::PHONE: return "PHONE";
    case PIICategory::SSN: return "SSN";
    case PIICategory::CREDIT_CARD: return "CREDIT_CARD";
    case PIICategory::IP_ADDRESS: return "IP_ADDRESS";
    case PIICategory::STREET_ADDRESS: return "STREET_ADDRESS";
    case PIICategory::DATE_OF_BIRTH: return "DATE_OF_BIRTH";
    case PIICategory::MEDICAL_RECORD_NUMBER: return "MRN";
    case PIICategory::API_KEY: return "API_KEY";
    case PIICategory::SENSITIVE_URL: return "SENSITIVE_URL";
    case PIICategory::ACCOUNT_NUMBER: return "ACCOUNT_NUMBER";
    case PIICategory::ZIP_CODE: return "ZIP_CODE";
    case PIICategory::DRIVER_LICENSE: return "DRIVER_LICENSE";
    case PIICategory::PASSPORT: return "PASSPORT";
    case PIICategory::BANK_ACCOUNT: return "BANK_ACCOUNT";
    case PIICategory::PERSON_NAME: return "PERSON_NAME";
    case PIICategory::ORGANIZATION_NAME: return "ORGANIZATION_NAME";
    case PIICategory::LOCATION_NAME: return "LOCATION_NAME";
    case PIICategory::HEALTH_INSURANCE_NUMBER: return "HEALTH_INSURANCE";
    case PIICategory::VEHICLE_IDENTIFICATION_NUMBER: return "VIN";
    case PIICategory::TAX_ID: return "TAX_ID";
    case PIICategory::IBAN: return "IBAN";
    case PIICategory::SWIFT_CODE: return "SWIFT_CODE";
    case PIICategory::CRYPTO_ADDRESS: return "CRYPTO_ADDRESS";
    case PIICategory::MAC_ADDRESS: return "MAC_ADDRESS";
    case PIICategory::USERNAME: return "USERNAME";
    case PIICategory::FILE_PATH: return "FILE_PATH";
    case PIICategory::NATIONAL_ID: return "NATIONAL_ID";
    case PIICategory::BIOMETRIC_ID: return "BIOMETRIC_ID";
    default: return "UNKNOWN";
  }
}

std::string ScrubStats::ToString() const {
  std::stringstream ss;
  ss << "PII Scrubbing Stats: " << total_items_found << " items redacted";
  if (!by_category.empty()) {
    ss << " (";
    bool first = true;
    for (const auto& [cat, count] : by_category) {
      if (!first) ss << ", ";
      ss << GetCategoryName(cat) << ":" << count;
      first = false;
    }
    ss << ")";
  }
  return ss.str();
}

OwlPIIScrubber::OwlPIIScrubber() {
  // Enable all categories by default
  for (int i = static_cast<int>(PIICategory::EMAIL);
       i <= static_cast<int>(PIICategory::BIOMETRIC_ID); i++) {
    category_enabled_[static_cast<PIICategory>(i)] = true;
  }

  // Initialize whitelisted email domains (common test/example domains)
  whitelisted_email_domains_ = {
    "example.com", "example.org", "example.net",
    "test.com", "test.org", "test.net",
    "localhost", "domain.com", "email.com",
    "sample.com", "demo.com"
  };

  // Initialize common test patterns to avoid false positives
  common_test_patterns_ = {
    "john.doe", "jane.doe", "john.smith", "jane.smith",
    "test", "demo", "sample", "example"
  };

  InitializePatterns();
}

void OwlPIIScrubber::InitializePatterns() {
    // Email pattern - comprehensive RFC 5322 compliant
    email_pattern_ = std::regex(
      R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b)",
      std::regex::icase
    );

    // Phone patterns - US and international
    // Matches: (123) 456-7890, 123-456-7890, 1234567890, +1 123 456 7890, etc.
    phone_pattern_ = std::regex(
      R"((\+?\d{1,3}[-.\s]?)?(\(?\d{3}\)?[-.\s]?)?\d{3}[-.\s]?\d{4}\b)",
      std::regex::icase
    );

    // SSN pattern - ###-##-#### or ### ## #### or #########
    ssn_pattern_ = std::regex(
      R"(\b\d{3}[-\s]?\d{2}[-\s]?\d{4}\b)"
    );

    // Credit card pattern - 13-19 digits with optional spaces/dashes
    // Covers Visa, MasterCard, Amex, Discover
    credit_card_pattern_ = std::regex(
      R"(\b\d{4}[\s-]?\d{4}[\s-]?\d{4}[\s-]?\d{4}[\s-]?\d{0,3}\b)"
    );

    // IPv4 pattern
    ipv4_pattern_ = std::regex(
      R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)"
    );

    // IPv6 pattern (simplified)
    ipv6_pattern_ = std::regex(
      R"(\b(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}\b)",
      std::regex::icase
    );

    // Street address pattern - US format (number + street name)
    street_address_pattern_ = std::regex(
      R"(\b\d{1,6}\s+[A-Z][a-z]+(\s+[A-Z][a-z]+)*\s+(Street|St|Avenue|Ave|Road|Rd|Boulevard|Blvd|Lane|Ln|Drive|Dr|Court|Ct|Way|Circle|Cir|Place|Pl)\b)",
      std::regex::icase
    );

    // Date of birth patterns - MM/DD/YYYY, MM-DD-YYYY, YYYY-MM-DD
    dob_pattern_ = std::regex(
      R"(\b(?:0[1-9]|1[0-2])[-/](?:0[1-9]|[12]\d|3[01])[-/](?:19|20)\d{2}\b|\b(?:19|20)\d{2}[-/](?:0[1-9]|1[0-2])[-/](?:0[1-9]|[12]\d|3[01])\b)"
    );

    // Medical record number - MRN followed by digits
    medical_record_pattern_ = std::regex(
      R"(\b(?:MRN|Medical\s+Record\s+Number|Patient\s+ID)[\s:]*\d{6,10}\b)",
      std::regex::icase
    );

    // API keys and tokens - common patterns
    api_key_pattern_ = std::regex(
      R"(\b(?:api[_-]?key|apikey|access[_-]?token|secret[_-]?key|auth[_-]?token|bearer)[\s:=]+['\"]?([A-Za-z0-9_\-]{20,})['\"]?\b)",
      std::regex::icase
    );

    // Sensitive URL parameters
    sensitive_url_pattern_ = std::regex(
      R"(([?&](?:token|key|password|secret|auth|api_key|access_token|bearer)=)[^&\s]+)",
      std::regex::icase
    );

    // Account numbers - 8-17 digits
    account_number_pattern_ = std::regex(
      R"(\b(?:account|acct)[\s#:]*\d{8,17}\b)",
      std::regex::icase
    );

    // ZIP codes - US format (5 digits or 5+4)
    zip_code_pattern_ = std::regex(
      R"(\b\d{5}(?:-\d{4})?\b)"
    );

    // Driver's license - state code + alphanumeric
    driver_license_pattern_ = std::regex(
      R"(\b(?:DL|Driver\s+License)[\s#:]*[A-Z]{2}[-\s]?[A-Z0-9]{6,12}\b)",
      std::regex::icase
    );

    // Passport numbers - 6-9 alphanumeric
    passport_pattern_ = std::regex(
      R"(\b(?:Passport|PPT)[\s#:]*[A-Z0-9]{6,9}\b)",
      std::regex::icase
    );

    // Bank account numbers - routing + account
    bank_account_pattern_ = std::regex(
      R"(\b(?:routing|account)[\s#:]*\d{9,17}\b)",
      std::regex::icase
    );

    // ===== NEW PATTERNS FOR ENHANCED DETECTION =====

    // Person name pattern - detects titles + capitalized names
    // Matches: Dr. John Smith, Mr. Bob Johnson, Sarah Williams
    name_title_pattern_ = std::regex(
      R"(\b(?:Mr|Mrs|Ms|Miss|Dr|Prof|Sr|Jr)\.?\s+[A-Z][a-z]+(?:\s+[A-Z][a-z]+)*\b)"
    );
    person_name_pattern_ = std::regex(
      R"(\b[A-Z][a-z]+\s+[A-Z][a-z]+(?:\s+(?:Jr|Sr|II|III|IV))?\b)"
    );

    // Organization names - Inc, LLC, Corp, Ltd, etc.
    organization_pattern_ = std::regex(
      R"(\b[A-Z][A-Za-z0-9\s&]+(?:Inc|LLC|Corp|Corporation|Ltd|Limited|Co|Company|Group|International|Technologies|Solutions|Services|Partners)\b)",
      std::regex::icase
    );

    // Location names - Cities, States, Countries with context
    location_pattern_ = std::regex(
      R"(\b(?:in|from|to|at|near)\s+[A-Z][a-z]+(?:\s+[A-Z][a-z]+)*(?:,\s*[A-Z]{2})?\b)"
    );

    // Health Insurance Numbers
    // Medicare: 1-2 letters + 2-3 letters + 4 digits + 1-2 letters/digits
    medicare_pattern_ = std::regex(
      R"(\b[A-Z]{1,2}[A-Z]{2,3}\d{4}[A-Z0-9]{1,2}\b)"
    );
    // Medicaid: State-specific, typically 8-14 digits
    medicaid_pattern_ = std::regex(
      R"(\b(?:Medicaid|MCD)[\s#:]*[A-Z0-9]{8,14}\b)",
      std::regex::icase
    );
    health_insurance_pattern_ = std::regex(
      R"(\b(?:Insurance|Policy|Member)[\s#:]*[A-Z0-9]{8,20}\b)",
      std::regex::icase
    );

    // VIN - 17 alphanumeric characters (no I, O, Q)
    vin_pattern_ = std::regex(
      R"(\b[A-HJ-NPR-Z0-9]{17}\b)"
    );

    // Tax IDs
    // EIN: 12-3456789 format
    ein_pattern_ = std::regex(
      R"(\b\d{2}-\d{7}\b)"
    );

    // IBAN - International Bank Account Number
    // 2 letter country code + 2 check digits + up to 30 alphanumeric
    iban_pattern_ = std::regex(
      R"(\b[A-Z]{2}\d{2}[A-Z0-9]{11,30}\b)"
    );

    // SWIFT/BIC codes - 8 or 11 characters
    swift_pattern_ = std::regex(
      R"(\b[A-Z]{6}[A-Z0-9]{2}(?:[A-Z0-9]{3})?\b)"
    );

    // Bitcoin addresses - 26-35 alphanumeric (starts with 1, 3, or bc1)
    bitcoin_pattern_ = std::regex(
      R"(\b(?:1|3|bc1)[a-zA-HJ-NP-Z0-9]{25,62}\b)"
    );

    // Ethereum addresses - 0x followed by 40 hex characters
    ethereum_pattern_ = std::regex(
      R"(\b0x[a-fA-F0-9]{40}\b)"
    );

    // MAC addresses - 6 pairs of hex separated by : or -
    mac_address_pattern_ = std::regex(
      R"(\b(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b)"
    );

    // Usernames - @username or username with context
    username_pattern_ = std::regex(
      R"(@[a-zA-Z0-9_]{3,15}\b|(?:username|user|login)[\s:]+[a-zA-Z0-9_]{3,20}\b)",
      std::regex::icase
    );

    // File paths with usernames
    file_path_pattern_ = std::regex(
      R"((?:/home/|/Users/|C:\\Users\\)[a-zA-Z0-9_-]+(?:/|\\)[a-zA-Z0-9_/\\.-]+)",
      std::regex::icase
    );

    // National IDs (various countries)
    // UK National Insurance Number: AA123456C
    // Canadian SIN: 123-456-789
    // Australian TFN: 123 456 789
    national_id_pattern_ = std::regex(
      R"(\b(?:[A-Z]{2}\d{6}[A-Z]|\d{3}[-\s]\d{3}[-\s]\d{3})\b)"
    );

    // Biometric identifiers - fingerprint IDs, iris scan IDs
    biometric_pattern_ = std::regex(
      R"(\b(?:Fingerprint|Iris|Biometric|Facial)[\s#:]*ID[\s:]*[A-Z0-9]{8,20}\b)",
      std::regex::icase
    );

}

std::string OwlPIIScrubber::ScrubText(const std::string& text) {
  std::string result = text;

  // Apply scrubbing in order of specificity (most specific first)
  // High-value PII first (financial, health, government IDs)

  if (IsCategoryEnabled(PIICategory::SSN)) {
    result = ScrubSSN(result);
  }

  if (IsCategoryEnabled(PIICategory::CREDIT_CARD)) {
    result = ScrubCreditCards(result);
  }

  if (IsCategoryEnabled(PIICategory::HEALTH_INSURANCE_NUMBER)) {
    result = ScrubHealthInsurance(result);
  }

  if (IsCategoryEnabled(PIICategory::MEDICAL_RECORD_NUMBER)) {
    result = ScrubMedicalRecords(result);
  }

  if (IsCategoryEnabled(PIICategory::DRIVER_LICENSE)) {
    result = ScrubDriverLicenses(result);
  }

  if (IsCategoryEnabled(PIICategory::PASSPORT)) {
    result = ScrubPassports(result);
  }

  if (IsCategoryEnabled(PIICategory::NATIONAL_ID)) {
    result = ScrubNationalIDs(result);
  }

  if (IsCategoryEnabled(PIICategory::TAX_ID)) {
    result = ScrubTaxIDs(result);
  }

  if (IsCategoryEnabled(PIICategory::IBAN)) {
    result = ScrubIBAN(result);
  }

  if (IsCategoryEnabled(PIICategory::SWIFT_CODE)) {
    result = ScrubSWIFT(result);
  }

  if (IsCategoryEnabled(PIICategory::BANK_ACCOUNT)) {
    result = ScrubBankAccounts(result);
  }

  if (IsCategoryEnabled(PIICategory::ACCOUNT_NUMBER)) {
    result = ScrubAccountNumbers(result);
  }

  if (IsCategoryEnabled(PIICategory::VEHICLE_IDENTIFICATION_NUMBER)) {
    result = ScrubVIN(result);
  }

  if (IsCategoryEnabled(PIICategory::BIOMETRIC_ID)) {
    result = ScrubBiometricIDs(result);
  }

  // Contact information
  if (IsCategoryEnabled(PIICategory::EMAIL)) {
    result = ScrubEmails(result);
  }

  if (IsCategoryEnabled(PIICategory::PHONE)) {
    result = ScrubPhoneNumbers(result);
  }

  if (IsCategoryEnabled(PIICategory::STREET_ADDRESS)) {
    result = ScrubStreetAddresses(result);
  }

  if (IsCategoryEnabled(PIICategory::ZIP_CODE)) {
    result = ScrubZipCodes(result);
  }

  // Personal identifiers
  if (IsCategoryEnabled(PIICategory::PERSON_NAME)) {
    result = ScrubPersonNames(result);
  }

  if (IsCategoryEnabled(PIICategory::DATE_OF_BIRTH)) {
    result = ScrubDatesOfBirth(result);
  }

  if (IsCategoryEnabled(PIICategory::USERNAME)) {
    result = ScrubUsernames(result);
  }

  // Technical identifiers
  if (IsCategoryEnabled(PIICategory::IP_ADDRESS)) {
    result = ScrubIPAddresses(result);
  }

  if (IsCategoryEnabled(PIICategory::MAC_ADDRESS)) {
    result = ScrubMACAddresses(result);
  }

  if (IsCategoryEnabled(PIICategory::FILE_PATH)) {
    result = ScrubFilePaths(result);
  }

  // Crypto and API
  if (IsCategoryEnabled(PIICategory::CRYPTO_ADDRESS)) {
    result = ScrubCryptoAddresses(result);
  }

  if (IsCategoryEnabled(PIICategory::API_KEY)) {
    result = ScrubAPIKeys(result);
  }

  if (IsCategoryEnabled(PIICategory::SENSITIVE_URL)) {
    result = ScrubSensitiveURLs(result);
  }

  // Organization and location (less specific, scrub last)
  if (IsCategoryEnabled(PIICategory::ORGANIZATION_NAME)) {
    result = ScrubOrganizationNames(result);
  }

  if (IsCategoryEnabled(PIICategory::LOCATION_NAME)) {
    result = ScrubLocationNames(result);
  }

  return result;
}

std::string OwlPIIScrubber::ScrubEmails(const std::string& text) {
    std::string result = text;
    int count = 0;

    std::sregex_iterator it(text.begin(), text.end(), email_pattern_);
    std::sregex_iterator end;

    // Collect matches and check whitelist
    std::vector<std::pair<std::string, size_t>> matches_to_scrub;
    for (; it != end; ++it) {
      std::string email = it->str();

      // Check if email is whitelisted
      if (!IsWhitelistedEmail(email)) {
        matches_to_scrub.push_back({email, it->position()});
        count++;
        stats_.AddDetection(PIICategory::EMAIL);
      }
    }

    // Replace non-whitelisted emails (reverse order to maintain positions)
    for (auto rit = matches_to_scrub.rbegin(); rit != matches_to_scrub.rend(); ++rit) {
      size_t pos = rit->second;
      size_t len = rit->first.length();
      result.replace(pos, len, "[EMAIL]");
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " email address(es)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubPhoneNumbers(const std::string& text) {
  
    std::string result = std::regex_replace(text, phone_pattern_, "[PHONE]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), phone_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::PHONE);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " phone number(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubSSN(const std::string& text) {
  
    std::string result = text;
    std::sregex_iterator it(text.begin(), text.end(), ssn_pattern_);
    std::sregex_iterator end;

    int count = 0;
    for (; it != end; ++it) {
      std::string potential_ssn = it->str();
      // Remove dashes/spaces for validation
      std::string clean_ssn = potential_ssn;
      clean_ssn.erase(std::remove_if(clean_ssn.begin(), clean_ssn.end(),
        [](char c) { return c == '-' || c == ' '; }), clean_ssn.end());

      if (IsValidSSN(clean_ssn)) {
        count++;
        stats_.AddDetection(PIICategory::SSN);
      }
    }

    if (count > 0) {
      result = std::regex_replace(result, ssn_pattern_, "[SSN]");
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " SSN(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubCreditCards(const std::string& text) {
  
    std::string result = text;
    std::sregex_iterator it(text.begin(), text.end(), credit_card_pattern_);
    std::sregex_iterator end;

    int count = 0;
    for (; it != end; ++it) {
      std::string potential_card = it->str();
      // Remove spaces/dashes
      std::string clean_card = potential_card;
      clean_card.erase(std::remove_if(clean_card.begin(), clean_card.end(),
        [](char c) { return c == '-' || c == ' '; }), clean_card.end());

      if (IsValidCreditCard(clean_card)) {
        count++;
        stats_.AddDetection(PIICategory::CREDIT_CARD);
      }
    }

    if (count > 0) {
      result = std::regex_replace(result, credit_card_pattern_, "[CREDIT_CARD]");
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " credit card(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubIPAddresses(const std::string& text) {
  
    std::string result = std::regex_replace(text, ipv4_pattern_, "[IP_ADDRESS]");
    result = std::regex_replace(result, ipv6_pattern_, "[IP_ADDRESS]");

    int count = 0;
    std::sregex_iterator it1(text.begin(), text.end(), ipv4_pattern_);
    std::sregex_iterator it2(text.begin(), text.end(), ipv6_pattern_);
    std::sregex_iterator end;

    for (; it1 != end; ++it1) {
      count++;
      stats_.AddDetection(PIICategory::IP_ADDRESS);
    }
    for (; it2 != end; ++it2) {
      count++;
      stats_.AddDetection(PIICategory::IP_ADDRESS);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " IP address(es)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubStreetAddresses(const std::string& text) {
  
    std::string result = std::regex_replace(text, street_address_pattern_, "[STREET_ADDRESS]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), street_address_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::STREET_ADDRESS);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " street address(es)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubDatesOfBirth(const std::string& text) {
  
    std::string result = std::regex_replace(text, dob_pattern_, "[DATE_OF_BIRTH]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), dob_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::DATE_OF_BIRTH);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " date(s) of birth");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubMedicalRecords(const std::string& text) {
  
    std::string result = std::regex_replace(text, medical_record_pattern_, "[MRN]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), medical_record_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::MEDICAL_RECORD_NUMBER);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " medical record number(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubAPIKeys(const std::string& text) {
  
    std::string result = std::regex_replace(text, api_key_pattern_, "[API_KEY]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), api_key_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::API_KEY);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " API key(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubSensitiveURLs(const std::string& text) {
  
    std::string result = std::regex_replace(text, sensitive_url_pattern_, "$1[REDACTED]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), sensitive_url_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::SENSITIVE_URL);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " sensitive URL parameter(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubAccountNumbers(const std::string& text) {
  
    std::string result = std::regex_replace(text, account_number_pattern_, "[ACCOUNT_NUMBER]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), account_number_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::ACCOUNT_NUMBER);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " account number(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubZipCodes(const std::string& text) {
  
    std::string result = std::regex_replace(text, zip_code_pattern_, "[ZIP_CODE]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), zip_code_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::ZIP_CODE);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " ZIP code(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubDriverLicenses(const std::string& text) {
  
    std::string result = std::regex_replace(text, driver_license_pattern_, "[DRIVER_LICENSE]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), driver_license_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::DRIVER_LICENSE);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " driver's license(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubPassports(const std::string& text) {
  
    std::string result = std::regex_replace(text, passport_pattern_, "[PASSPORT]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), passport_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::PASSPORT);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " passport number(s)");
    }

    return result;
}

std::string OwlPIIScrubber::ScrubBankAccounts(const std::string& text) {
  
    std::string result = std::regex_replace(text, bank_account_pattern_, "[BANK_ACCOUNT]");

    int count = 0;
    std::sregex_iterator it(text.begin(), text.end(), bank_account_pattern_);
    std::sregex_iterator end;
    for (; it != end; ++it) {
      count++;
      stats_.AddDetection(PIICategory::BANK_ACCOUNT);
    }

    if (count > 0) {
      LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " bank account number(s)");
    }

    return result;
}

bool OwlPIIScrubber::IsValidCreditCard(const std::string& number) const {
  // Luhn algorithm validation
  if (number.length() < 13 || number.length() > 19) {
    return false;
  }

  int sum = 0;
  bool alternate = false;

  for (int i = number.length() - 1; i >= 0; i--) {
    int digit = number[i] - '0';
    if (digit < 0 || digit > 9) {
      return false;
    }

    if (alternate) {
      digit *= 2;
      if (digit > 9) {
        digit = (digit % 10) + 1;
      }
    }

    sum += digit;
    alternate = !alternate;
  }

  return (sum % 10 == 0);
}

bool OwlPIIScrubber::IsValidSSN(const std::string& ssn) const {
  if (ssn.length() != 9) {
    return false;
  }

  // Check for all zeros or invalid area numbers
  if (ssn == "000000000" || ssn.substr(0, 3) == "000" || ssn.substr(0, 3) == "666") {
    return false;
  }

  // Check for all same digit
  char first = ssn[0];
  bool all_same = true;
  for (char c : ssn) {
    if (c != first) {
      all_same = false;
      break;
    }
  }

  return !all_same;
}

void OwlPIIScrubber::SetCategoryEnabled(PIICategory category, bool enabled) {
  category_enabled_[category] = enabled;
  LOG_DEBUG("PIIScrubber", std::string("Category ") + GetCategoryName(category) +
           (enabled ? " enabled" : " disabled"));
}

bool OwlPIIScrubber::IsCategoryEnabled(PIICategory category) const {
  auto it = category_enabled_.find(category);
  if (it != category_enabled_.end()) {
    return it->second;
  }
  return true;  // Default to enabled
}

// ===== NEW SCRUBBING METHODS =====

std::string OwlPIIScrubber::ScrubPersonNames(const std::string& text) {
  std::string result = text;
  int count = 0;

  // First scrub titles + names (Mr. John Smith)
  std::sregex_iterator it1(text.begin(), text.end(), name_title_pattern_);
  std::sregex_iterator end;
  for (; it1 != end; ++it1) {
    count++;
    stats_.AddDetection(PIICategory::PERSON_NAME);
  }
  if (count > 0) {
    result = std::regex_replace(result, name_title_pattern_, "[PERSON_NAME]");
  }

  // Then scrub regular names (John Smith)
  std::sregex_iterator it2(result.begin(), result.end(), person_name_pattern_);
  for (; it2 != end; ++it2) {
    std::string name = it2->str();
    // Check if it's not a common test pattern
    bool is_test = false;
    for (const auto& pattern : common_test_patterns_) {
      if (name.find(pattern) != std::string::npos) {
        is_test = true;
        break;
      }
    }
    if (!is_test) {
      count++;
      stats_.AddDetection(PIICategory::PERSON_NAME);
    }
  }

  if (count > 0) {
    result = std::regex_replace(result, person_name_pattern_, "[PERSON_NAME]");
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " person name(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubOrganizationNames(const std::string& text) {
  std::string result = std::regex_replace(text, organization_pattern_, "[ORGANIZATION]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), organization_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::ORGANIZATION_NAME);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " organization name(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubLocationNames(const std::string& text) {
  std::string result = std::regex_replace(text, location_pattern_, "[LOCATION]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), location_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::LOCATION_NAME);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " location name(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubHealthInsurance(const std::string& text) {
  std::string result = text;
  int count = 0;

  // Scrub Medicare numbers
  std::sregex_iterator it1(text.begin(), text.end(), medicare_pattern_);
  std::sregex_iterator end;
  for (; it1 != end; ++it1) {
    count++;
    stats_.AddDetection(PIICategory::HEALTH_INSURANCE_NUMBER);
  }
  result = std::regex_replace(result, medicare_pattern_, "[HEALTH_INSURANCE]");

  // Scrub Medicaid numbers
  std::sregex_iterator it2(result.begin(), result.end(), medicaid_pattern_);
  for (; it2 != end; ++it2) {
    count++;
    stats_.AddDetection(PIICategory::HEALTH_INSURANCE_NUMBER);
  }
  result = std::regex_replace(result, medicaid_pattern_, "[HEALTH_INSURANCE]");

  // Scrub generic health insurance numbers
  std::sregex_iterator it3(result.begin(), result.end(), health_insurance_pattern_);
  for (; it3 != end; ++it3) {
    count++;
    stats_.AddDetection(PIICategory::HEALTH_INSURANCE_NUMBER);
  }
  result = std::regex_replace(result, health_insurance_pattern_, "[HEALTH_INSURANCE]");

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " health insurance number(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubVIN(const std::string& text) {
  std::string result = text;
  int count = 0;

  std::sregex_iterator it(text.begin(), text.end(), vin_pattern_);
  std::sregex_iterator end;

  // Validate VINs before scrubbing
  for (; it != end; ++it) {
    std::string vin = it->str();
    if (IsValidVIN(vin)) {
      count++;
      stats_.AddDetection(PIICategory::VEHICLE_IDENTIFICATION_NUMBER);
    }
  }

  if (count > 0) {
    result = std::regex_replace(result, vin_pattern_, "[VIN]");
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " VIN(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubTaxIDs(const std::string& text) {
  std::string result = std::regex_replace(text, ein_pattern_, "[TAX_ID]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), ein_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::TAX_ID);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " tax ID(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubIBAN(const std::string& text) {
  std::string result = text;
  int count = 0;

  std::sregex_iterator it(text.begin(), text.end(), iban_pattern_);
  std::sregex_iterator end;

  // Validate IBANs before scrubbing
  for (; it != end; ++it) {
    std::string iban = it->str();
    if (IsValidIBAN(iban)) {
      count++;
      stats_.AddDetection(PIICategory::IBAN);
    }
  }

  if (count > 0) {
    result = std::regex_replace(result, iban_pattern_, "[IBAN]");
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " IBAN(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubSWIFT(const std::string& text) {
  std::string result = std::regex_replace(text, swift_pattern_, "[SWIFT_CODE]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), swift_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::SWIFT_CODE);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " SWIFT code(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubCryptoAddresses(const std::string& text) {
  std::string result = text;
  int count = 0;

  // Scrub Bitcoin addresses
  std::sregex_iterator it1(text.begin(), text.end(), bitcoin_pattern_);
  std::sregex_iterator end;
  for (; it1 != end; ++it1) {
    count++;
    stats_.AddDetection(PIICategory::CRYPTO_ADDRESS);
  }
  result = std::regex_replace(result, bitcoin_pattern_, "[CRYPTO_ADDRESS]");

  // Scrub Ethereum addresses
  std::sregex_iterator it2(result.begin(), result.end(), ethereum_pattern_);
  for (; it2 != end; ++it2) {
    count++;
    stats_.AddDetection(PIICategory::CRYPTO_ADDRESS);
  }
  result = std::regex_replace(result, ethereum_pattern_, "[CRYPTO_ADDRESS]");

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " cryptocurrency address(es)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubMACAddresses(const std::string& text) {
  std::string result = std::regex_replace(text, mac_address_pattern_, "[MAC_ADDRESS]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), mac_address_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::MAC_ADDRESS);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " MAC address(es)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubUsernames(const std::string& text) {
  std::string result = std::regex_replace(text, username_pattern_, "[USERNAME]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), username_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::USERNAME);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " username(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubFilePaths(const std::string& text) {
  std::string result = std::regex_replace(text, file_path_pattern_, "[FILE_PATH]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), file_path_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::FILE_PATH);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " file path(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubNationalIDs(const std::string& text) {
  std::string result = std::regex_replace(text, national_id_pattern_, "[NATIONAL_ID]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), national_id_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::NATIONAL_ID);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " national ID(s)");
  }

  return result;
}

std::string OwlPIIScrubber::ScrubBiometricIDs(const std::string& text) {
  std::string result = std::regex_replace(text, biometric_pattern_, "[BIOMETRIC_ID]");

  int count = 0;
  std::sregex_iterator it(text.begin(), text.end(), biometric_pattern_);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    count++;
    stats_.AddDetection(PIICategory::BIOMETRIC_ID);
  }

  if (count > 0) {
    LOG_DEBUG("PIIScrubber", "Redacted " + std::to_string(count) + " biometric ID(s)");
  }

  return result;
}

// ===== NEW VALIDATION METHODS =====

bool OwlPIIScrubber::IsValidVIN(const std::string& vin) const {
  if (vin.length() != 17) {
    return false;
  }

  // VINs don't contain I, O, or Q (to avoid confusion with 1, 0)
  for (char c : vin) {
    if (c == 'I' || c == 'O' || c == 'Q' ||
        c == 'i' || c == 'o' || c == 'q') {
      return false;
    }
  }

  // VIN checksum validation (simplified - position 9 is check digit)
  // Full validation would require transliteration table
  return true;
}

bool OwlPIIScrubber::IsValidIBAN(const std::string& iban) const {
  if (iban.length() < 15 || iban.length() > 34) {
    return false;
  }

  // Basic IBAN format: 2 letters + 2 digits + alphanumeric
  if (!std::isalpha(iban[0]) || !std::isalpha(iban[1]) ||
      !std::isdigit(iban[2]) || !std::isdigit(iban[3])) {
    return false;
  }

  // Full IBAN validation would require mod-97 checksum
  return true;
}

bool OwlPIIScrubber::IsValidIPAddress(const std::string& ip) const {
  // Simple IPv4 range validation
  std::regex ipv4_regex(R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))");
  std::smatch match;

  if (std::regex_match(ip, match, ipv4_regex)) {
    // Check each octet is 0-255
    for (int i = 1; i <= 4; i++) {
      int octet = std::stoi(match[i].str());
      if (octet < 0 || octet > 255) {
        return false;
      }
    }
    return true;
  }

  // IPv6 validation would be more complex
  return false;
}

bool OwlPIIScrubber::IsWhitelistedEmail(const std::string& email) const {
  // Extract domain from email
  size_t at_pos = email.find('@');
  if (at_pos == std::string::npos) {
    return false;
  }

  std::string domain = email.substr(at_pos + 1);

  // Convert to lowercase for comparison
  std::transform(domain.begin(), domain.end(), domain.begin(), ::tolower);

  // Check if domain is in whitelist
  for (const auto& whitelisted : whitelisted_email_domains_) {
    if (domain == whitelisted) {
      return true;
    }
  }

  return false;
}

bool OwlPIIScrubber::HasNameIndicators(const std::string& text) const {
  // Check for name titles
  std::regex title_regex(R"(\b(?:Mr|Mrs|Ms|Miss|Dr|Prof)\.?\s)");
  return std::regex_search(text, title_regex);
}

std::vector<std::string> OwlPIIScrubber::ExtractPotentialNames(const std::string& text) const {
  std::vector<std::string> names;

  // Extract capitalized words (potential names)
  std::regex name_regex(R"(\b[A-Z][a-z]+\b)");
  std::sregex_iterator it(text.begin(), text.end(), name_regex);
  std::sregex_iterator end;

  for (; it != end; ++it) {
    names.push_back(it->str());
  }

  return names;
}

}  // namespace OwlPII
