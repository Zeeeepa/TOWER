/**
 * Owl Browser License Manager
 *
 * This module provides hardware-bound, cryptographically signed license validation.
 * Uses RSA-2048 for signing and AES-256 for data encryption.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>

namespace olib {
namespace license {

// License file magic bytes for format validation
constexpr uint32_t LICENSE_MAGIC = 0x4F4C4943; // "OLIC"
constexpr uint32_t LICENSE_VERSION = 2;  // Version 2: Added extended metadata fields

// License types
enum class LicenseType : uint8_t {
    TRIAL = 0,
    STARTER = 1,       // Monthly subscription ($1,999/mo, 3 seats)
    BUSINESS = 2,      // One-time $19,999 + optional maintenance ($3,999/mo, 10 seats, 1 year)
    ENTERPRISE = 3,    // One-time $49,999 + optional maintenance ($9,999/mo, 50 seats, 1 year)
    DEVELOPER = 4,
    SUBSCRIPTION = 5,  // Subscription-based license requiring periodic server validation
};

// Support tier for SLA
enum class SupportTier : uint8_t {
    NONE = 0,
    BASIC = 1,
    STANDARD = 2,
    PREMIUM = 3,
    ENTERPRISE = 4,
};

// Subscription status (for SUBSCRIPTION type licenses)
enum class SubscriptionStatus : uint8_t {
    UNKNOWN = 0,        // Initial state, needs server check
    ACTIVE = 1,         // Subscription is active
    INACTIVE = 2,       // Subscription was canceled or expired
    PENDING = 3,        // Waiting for server response
    SERVER_ERROR = 4,   // Could not reach server (grace period applies)
};

// License status codes
enum class LicenseStatus {
    VALID = 0,
    EXPIRED = 1,
    INVALID_SIGNATURE = 2,
    CORRUPTED = 3,
    NOT_FOUND = 4,
    HARDWARE_MISMATCH = 5,
    SEAT_EXCEEDED = 6,
    REVOKED = 7,
    TAMPERED = 8,
    CLOCK_MANIPULATED = 9,
    DEBUG_DETECTED = 10,
    SUBSCRIPTION_INACTIVE = 11,  // Subscription was canceled or not renewed
    SUBSCRIPTION_CHECK_FAILED = 12, // Could not verify subscription (grace period may apply)
    INTERNAL_ERROR = 99,
};

// License data structure (plaintext before encryption/signing)
struct LicenseData {
    // Header
    uint32_t magic;
    uint32_t version;

    // Licensee Information
    std::string license_id;           // Unique license identifier (UUID)
    std::string name;                 // Licensee name
    std::string organization;         // Organization/Company name
    std::string email;                // Contact email

    // License Terms
    LicenseType type;
    uint32_t max_seats;               // Number of allowed simultaneous devices
    int64_t issue_timestamp;          // Unix timestamp when license was issued
    int64_t expiry_timestamp;         // Unix timestamp when license expires (0 = perpetual)

    // Features (bitmask for feature flags)
    uint64_t feature_flags;

    // Hardware binding (optional)
    bool hardware_bound;
    std::string hardware_fingerprint; // SHA-256 of hardware identifiers

    // Custom fields (JSON string for extensibility)
    std::string custom_data;

    // Metadata
    std::string issuer;               // Who issued this license
    std::string notes;                // Internal notes

    // Subscription fields (only used when type == SUBSCRIPTION)
    int64_t activation_timestamp;     // When the subscription was first activated
    int64_t last_check_timestamp;     // Last successful subscription check
    int64_t next_check_timestamp;     // When the next subscription check is due
    uint32_t grace_period_days;       // Days to allow if server unreachable (default 7)

    // === Version 2 Extended Metadata ===

    // Version Control
    std::string min_browser_version;      // Minimum browser version required (e.g., "1.0.0")
    std::string max_browser_version;      // Maximum browser version allowed (empty = no limit)

    // Geographic/Compliance
    std::string allowed_regions;          // Comma-separated region codes (e.g., "US,EU,CA") - empty = all
    std::string export_control;           // Export control classification

    // Usage Tracking
    uint32_t total_activations;           // Counter of total activations
    std::string last_device_name;         // Name of last activated device

    // Business Metadata
    std::string customer_id;              // Link to customer in server database
    std::string plan_id;                  // Link to billing plan
    std::string order_id;                 // Purchase/order reference
    std::string invoice_id;               // Invoice reference
    std::string reseller_id;              // Reseller/partner ID if sold through channel

    // Support/SLA
    SupportTier support_tier;             // Support tier level
    int64_t support_expiry_timestamp;     // When support expires (may differ from license)

    // Security
    std::string revocation_check_url;     // URL to check revocation status (override default)
    std::string issued_ip;                // IP address where license was issued

    // Maintenance
    bool maintenance_included;            // Whether maintenance/updates are included
    int64_t maintenance_expiry_timestamp; // When maintenance expires

    LicenseData();
    std::vector<uint8_t> Serialize() const;
    static LicenseData Deserialize(const std::vector<uint8_t>& data);
    bool IsSubscription() const { return type == LicenseType::SUBSCRIPTION; }

    // Version 2 helper methods
    bool IsVersionCompatible(const std::string& browser_version) const;
    bool IsRegionAllowed(const std::string& region_code) const;
    bool IsSupportActive() const;
    bool IsMaintenanceActive() const;
};

// Hardware fingerprint generator
class HardwareFingerprint {
public:
    static std::string Generate();
    static bool Verify(const std::string& expected);

private:
    static std::string GetMachineId();
    static std::string GetCPUInfo();
    static std::string GetMACAddress();
    static std::string GetDiskSerial();
};

// License file structure
struct LicenseFile {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;

    // Encrypted license data (AES-256-GCM)
    std::vector<uint8_t> encrypted_data;
    std::vector<uint8_t> iv;           // Initialization vector
    std::vector<uint8_t> auth_tag;     // Authentication tag

    // RSA-2048 signature of encrypted_data
    std::vector<uint8_t> signature;

    // Checksum of the entire structure (for quick validation)
    uint32_t checksum;

    bool SaveToFile(const std::string& path) const;
    static std::unique_ptr<LicenseFile> LoadFromFile(const std::string& path);
};

// Secure subscription state storage
// Uses hardware-bound encryption to prevent tampering
// State file is encrypted with AES-256 using a key derived from hardware fingerprint
class SubscriptionStateStorage {
public:
    struct SubscriptionState {
        std::string license_id;           // License ID for verification
        SubscriptionStatus status;        // Current subscription status
        int64_t activation_timestamp;     // First activation time
        int64_t last_check_timestamp;     // Last successful server check
        int64_t next_check_timestamp;     // Next scheduled check (monthly from activation)
        uint32_t check_interval_days;     // Days between checks (default 30)
        uint32_t consecutive_failures;    // Failed server check count
        uint32_t grace_period_days;       // Grace period for offline use
        std::string server_signature;     // Signature from last valid server response
        std::vector<uint8_t> integrity_hash; // Hash for tamper detection
    };

    static std::string GetStatePath();
    static bool SaveState(const SubscriptionState& state);
    static std::unique_ptr<SubscriptionState> LoadState();
    static bool DeleteState();

private:
    // Derive encryption key from hardware fingerprint + salt
    static std::vector<uint8_t> DeriveStateKey();
    // Compute integrity hash over state data
    static std::vector<uint8_t> ComputeIntegrityHash(const SubscriptionState& state);
    // Verify state hasn't been tampered with
    static bool VerifyIntegrity(const SubscriptionState& state);
};

// License validator - singleton with distributed verification
class LicenseManager {
public:
    static LicenseManager* GetInstance();

    // License management
    LicenseStatus AddLicense(const std::string& license_path);
    LicenseStatus RemoveLicense();
    LicenseStatus Validate();

    // Status queries
    bool IsValid() const;
    LicenseStatus GetStatus() const;
    std::string GetStatusMessage() const;
    const LicenseData* GetLicenseData() const;

    // License info (for display)
    std::string GetLicenseInfo() const;  // JSON format

    // Path management
    std::string GetLicensePath() const;
    static std::string GetDefaultLicensePath();

    // Verification (called from multiple points)
    // Returns false if license invalid (browser should fail gracefully)
    bool VerifyIntegrity();

    // Anti-tampering
    bool DetectDebugger();
    bool VerifyCodeIntegrity();

    // Subscription management
    SubscriptionStatus GetSubscriptionStatus() const;
    bool IsSubscriptionDue() const;  // Check if monthly check is needed
    LicenseStatus CheckSubscription();  // Perform server check
    int64_t GetNextCheckTimestamp() const;
    int64_t GetActivationTimestamp() const;

private:
    LicenseManager();
    ~LicenseManager();

    static LicenseManager* instance_;
    static std::mutex instance_mutex_;

    // License state (encrypted in memory)
    struct LicenseState;
    std::unique_ptr<LicenseState> state_;

    // Verification methods
    bool VerifySignature(const LicenseFile& file);
    bool DecryptLicenseData(const LicenseFile& file, LicenseData& data);
    bool ValidateExpiry(const LicenseData& data);
    bool ValidateHardware(const LicenseData& data);
    bool ValidateSeats(const LicenseData& data);

    // Anti-tamper checks
    int64_t GetSecureTimestamp() const;
    bool CheckClockManipulation();

    // Distributed verification points
    void SchedulePeriodicCheck();
    void PerformBackgroundCheck();

    // Key management (public key only - for verification)
    static std::vector<uint8_t> GetPublicKey();

    // Subscription validation
    bool ValidateSubscription(const LicenseData& data);
    bool CheckSubscriptionServer(const std::string& license_id, bool& is_active);
    int64_t CalculateNextCheckTimestamp(int64_t activation_timestamp) const;
    bool IsInGracePeriod() const;
    void UpdateSubscriptionState(SubscriptionStatus status, bool server_check_success);

    // Universal license activation (all types)
    bool ActivateLicenseWithServer(const std::string& license_id, int& seat_count, int& max_seats);

    // Daily license validation check (for all license types)
    bool PerformDailyLicenseCheck(const LicenseData& data);
    bool IsDailyCheckDue() const;
    void UpdateDailyCheckState(bool success);

    // SSL/TLS validation for license server
    static bool ValidateServerCertificate(const std::string& hostname);
    static std::string GetLicenseServerUrl();

    // Tampering detection reporting
    // Reports tampering events to the license server asynchronously
    void ReportTamperingToServer(LicenseStatus tampering_type, const std::string& details = "");
};

// Crypto utilities
class LicenseCrypto {
public:
    // RSA operations
    static bool VerifyRSA(const std::vector<uint8_t>& data,
                          const std::vector<uint8_t>& signature,
                          const std::vector<uint8_t>& public_key);

    static bool SignRSA(const std::vector<uint8_t>& data,
                        const std::vector<uint8_t>& private_key,
                        std::vector<uint8_t>& signature);

    // AES-256-GCM operations
    static bool Encrypt(const std::vector<uint8_t>& plaintext,
                        const std::vector<uint8_t>& key,
                        std::vector<uint8_t>& ciphertext,
                        std::vector<uint8_t>& iv,
                        std::vector<uint8_t>& tag);

    static bool Decrypt(const std::vector<uint8_t>& ciphertext,
                        const std::vector<uint8_t>& key,
                        const std::vector<uint8_t>& iv,
                        const std::vector<uint8_t>& tag,
                        std::vector<uint8_t>& plaintext);

    // Hash functions
    static std::vector<uint8_t> SHA256(const std::vector<uint8_t>& data);
    static std::string SHA256Hex(const std::string& data);

    // Random generation
    static std::vector<uint8_t> RandomBytes(size_t length);
    static std::string GenerateUUID();

    // Key derivation
    static std::vector<uint8_t> DeriveKey(const std::string& password,
                                           const std::vector<uint8_t>& salt);
};

// Inline helper for quick license check (used throughout codebase)
inline bool QuickLicenseCheck() {
    // This function is intentionally kept simple but will be called from many places
    // The actual state is encrypted and verified in the LicenseManager
    return LicenseManager::GetInstance()->IsValid();
}

// Status to string conversion
const char* LicenseStatusToString(LicenseStatus status);

}  // namespace license
}  // namespace olib
