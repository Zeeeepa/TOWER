/**
 * Owl Browser License Generator
 *
 * This tool generates cryptographically signed license files for Owl Browser.
 * Uses RSA-2048 for signing and AES-256-GCM for encryption.
 *
 * IMPORTANT: Keep the private key secure! Only authorized personnel should have access.
 *
 * Build:
 *   macOS: clang++ -std=c++17 -o license_generator license_generator.cpp \
 *          -framework Security -framework CoreFoundation -framework IOKit
 *   Linux: g++ -std=c++17 -o license_generator license_generator.cpp \
 *          -lssl -lcrypto
 *
 * Usage:
 *   ./license_generator generate --name "John Doe" --org "Acme Corp" \
 *                       --email "john@acme.com" --type business \
 *                       --seats 10 --expiry 365 --output license.olic
 *
 *   ./license_generator info license.olic
 *   ./license_generator verify license.olic
 *   ./license_generator keygen  # Generate new RSA key pair
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <map>
#include <random>
#include <memory>
#include <unistd.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonRandom.h>
#include <Security/Security.h>
#include <IOKit/IOKitLib.h>
#else
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#endif

// License file constants
constexpr uint32_t LICENSE_MAGIC = 0x4F4C4943; // "OLIC"
constexpr uint32_t LICENSE_VERSION = 2;  // Version 2: Added extended metadata fields

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

struct LicenseData {
    uint32_t magic = LICENSE_MAGIC;
    uint32_t version = LICENSE_VERSION;
    std::string license_id;
    std::string name;
    std::string organization;
    std::string email;
    LicenseType type = LicenseType::TRIAL;
    uint32_t max_seats = 1;
    int64_t issue_timestamp = 0;
    int64_t expiry_timestamp = 0;
    uint64_t feature_flags = 0;
    bool hardware_bound = false;
    std::string hardware_fingerprint;
    std::string custom_data;
    std::string issuer;
    std::string notes;

    // Subscription fields (only used when type == SUBSCRIPTION)
    int64_t activation_timestamp = 0;     // When the subscription was first activated
    int64_t last_check_timestamp = 0;     // Last successful subscription check
    int64_t next_check_timestamp = 0;     // When the next subscription check is due
    uint32_t grace_period_days = 7;       // Days to allow if server unreachable

    // === Version 2 Extended Metadata ===

    // Version Control
    std::string min_browser_version;      // Minimum browser version required (e.g., "1.0.0")
    std::string max_browser_version;      // Maximum browser version allowed (empty = no limit)

    // Geographic/Compliance
    std::string allowed_regions;          // Comma-separated region codes (e.g., "US,EU,CA") - empty = all
    std::string export_control;           // Export control classification

    // Usage Tracking
    uint32_t total_activations = 0;       // Counter of total activations
    std::string last_device_name;         // Name of last activated device

    // Business Metadata
    std::string customer_id;              // Link to customer in server database
    std::string plan_id;                  // Link to billing plan
    std::string order_id;                 // Purchase/order reference
    std::string invoice_id;               // Invoice reference
    std::string reseller_id;              // Reseller/partner ID if sold through channel

    // Support/SLA
    SupportTier support_tier = SupportTier::NONE;
    int64_t support_expiry_timestamp = 0; // When support expires (may differ from license)

    // Security
    std::string revocation_check_url;     // URL to check revocation status (override default)
    std::string issued_ip;                // IP address where license was issued

    // Maintenance
    bool maintenance_included = false;    // Whether maintenance/updates are included
    int64_t maintenance_expiry_timestamp = 0; // When maintenance expires

    std::vector<uint8_t> Serialize() const;
    static LicenseData Deserialize(const std::vector<uint8_t>& data);
    std::string ToJSON() const;
    bool IsSubscription() const { return type == LicenseType::SUBSCRIPTION; }
};

struct LicenseFile {
    uint32_t magic = LICENSE_MAGIC;
    uint32_t version = LICENSE_VERSION;
    uint32_t flags = 0;
    std::vector<uint8_t> encrypted_data;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> auth_tag;
    std::vector<uint8_t> signature;
    uint32_t checksum = 0;

    bool SaveToFile(const std::string& path) const;
    static std::unique_ptr<LicenseFile> LoadFromFile(const std::string& path);
};

// ============================================================================
// Crypto Utilities
// ============================================================================

class Crypto {
public:
    static std::vector<uint8_t> SHA256(const std::vector<uint8_t>& data);
    static std::string SHA256Hex(const std::string& data);
    static std::vector<uint8_t> RandomBytes(size_t length);
    static std::string GenerateUUID();

    static bool GenerateRSAKeyPair(std::string& private_key_pem, std::string& public_key_pem);

    static bool SignRSA(const std::vector<uint8_t>& data,
                        const std::string& private_key_pem,
                        std::vector<uint8_t>& signature);

    static bool VerifyRSA(const std::vector<uint8_t>& data,
                          const std::vector<uint8_t>& signature,
                          const std::string& public_key_pem);

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

    static std::vector<uint8_t> DeriveAESKey(const std::string& public_key_pem);
};

// ============================================================================
// Implementation
// ============================================================================

std::vector<uint8_t> LicenseData::Serialize() const {
    std::ostringstream ss;

    ss.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ss.write(reinterpret_cast<const char*>(&version), sizeof(version));

    auto writeString = [&ss](const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.length());
        ss.write(reinterpret_cast<const char*>(&len), sizeof(len));
        ss.write(str.data(), len);
    };

    writeString(license_id);
    writeString(name);
    writeString(organization);
    writeString(email);

    uint8_t type_byte = static_cast<uint8_t>(type);
    ss.write(reinterpret_cast<const char*>(&type_byte), sizeof(type_byte));
    ss.write(reinterpret_cast<const char*>(&max_seats), sizeof(max_seats));
    ss.write(reinterpret_cast<const char*>(&issue_timestamp), sizeof(issue_timestamp));
    ss.write(reinterpret_cast<const char*>(&expiry_timestamp), sizeof(expiry_timestamp));
    ss.write(reinterpret_cast<const char*>(&feature_flags), sizeof(feature_flags));

    uint8_t hw_bound = hardware_bound ? 1 : 0;
    ss.write(reinterpret_cast<const char*>(&hw_bound), sizeof(hw_bound));
    writeString(hardware_fingerprint);
    writeString(custom_data);
    writeString(issuer);
    writeString(notes);

    // Subscription fields (version 1+)
    ss.write(reinterpret_cast<const char*>(&activation_timestamp), sizeof(activation_timestamp));
    ss.write(reinterpret_cast<const char*>(&last_check_timestamp), sizeof(last_check_timestamp));
    ss.write(reinterpret_cast<const char*>(&next_check_timestamp), sizeof(next_check_timestamp));
    ss.write(reinterpret_cast<const char*>(&grace_period_days), sizeof(grace_period_days));

    // === Version 2 Extended Metadata ===
    // Version Control
    writeString(min_browser_version);
    writeString(max_browser_version);

    // Geographic/Compliance
    writeString(allowed_regions);
    writeString(export_control);

    // Usage Tracking
    ss.write(reinterpret_cast<const char*>(&total_activations), sizeof(total_activations));
    writeString(last_device_name);

    // Business Metadata
    writeString(customer_id);
    writeString(plan_id);
    writeString(order_id);
    writeString(invoice_id);
    writeString(reseller_id);

    // Support/SLA
    uint8_t support_tier_byte = static_cast<uint8_t>(support_tier);
    ss.write(reinterpret_cast<const char*>(&support_tier_byte), sizeof(support_tier_byte));
    ss.write(reinterpret_cast<const char*>(&support_expiry_timestamp), sizeof(support_expiry_timestamp));

    // Security
    writeString(revocation_check_url);
    writeString(issued_ip);

    // Maintenance
    uint8_t maint_included = maintenance_included ? 1 : 0;
    ss.write(reinterpret_cast<const char*>(&maint_included), sizeof(maint_included));
    ss.write(reinterpret_cast<const char*>(&maintenance_expiry_timestamp), sizeof(maintenance_expiry_timestamp));

    std::string data = ss.str();
    return std::vector<uint8_t>(data.begin(), data.end());
}

LicenseData LicenseData::Deserialize(const std::vector<uint8_t>& data) {
    LicenseData lic;

    if (data.size() < sizeof(uint32_t) * 2) {
        throw std::runtime_error("License data too short");
    }

    size_t pos = 0;

    auto readUint32 = [&data, &pos]() -> uint32_t {
        if (pos + sizeof(uint32_t) > data.size()) throw std::runtime_error("Read overflow");
        uint32_t val;
        std::memcpy(&val, &data[pos], sizeof(uint32_t));
        pos += sizeof(uint32_t);
        return val;
    };

    auto readInt64 = [&data, &pos]() -> int64_t {
        if (pos + sizeof(int64_t) > data.size()) throw std::runtime_error("Read overflow");
        int64_t val;
        std::memcpy(&val, &data[pos], sizeof(int64_t));
        pos += sizeof(int64_t);
        return val;
    };

    auto readUint64 = [&data, &pos]() -> uint64_t {
        if (pos + sizeof(uint64_t) > data.size()) throw std::runtime_error("Read overflow");
        uint64_t val;
        std::memcpy(&val, &data[pos], sizeof(uint64_t));
        pos += sizeof(uint64_t);
        return val;
    };

    auto readUint8 = [&data, &pos]() -> uint8_t {
        if (pos + sizeof(uint8_t) > data.size()) throw std::runtime_error("Read overflow");
        uint8_t val = data[pos];
        pos += sizeof(uint8_t);
        return val;
    };

    auto readString = [&data, &pos, &readUint32]() -> std::string {
        uint32_t len = readUint32();
        if (pos + len > data.size()) throw std::runtime_error("String read overflow");
        std::string str(reinterpret_cast<const char*>(&data[pos]), len);
        pos += len;
        return str;
    };

    lic.magic = readUint32();
    lic.version = readUint32();

    if (lic.magic != LICENSE_MAGIC) {
        throw std::runtime_error("Invalid license magic");
    }

    lic.license_id = readString();
    lic.name = readString();
    lic.organization = readString();
    lic.email = readString();

    lic.type = static_cast<LicenseType>(readUint8());
    lic.max_seats = readUint32();
    lic.issue_timestamp = readInt64();
    lic.expiry_timestamp = readInt64();
    lic.feature_flags = readUint64();

    lic.hardware_bound = readUint8() != 0;
    lic.hardware_fingerprint = readString();
    lic.custom_data = readString();
    lic.issuer = readString();
    lic.notes = readString();

    // Subscription fields (version 1+) - read if data available
    if (pos + sizeof(int64_t) * 3 + sizeof(uint32_t) <= data.size()) {
        lic.activation_timestamp = readInt64();
        lic.last_check_timestamp = readInt64();
        lic.next_check_timestamp = readInt64();
        lic.grace_period_days = readUint32();
    }

    // Version 2 extended metadata - read if version >= 2 and data available
    if (lic.version >= 2 && pos < data.size()) {
        // Helper to safely read string only if data available
        auto safeReadString = [&]() -> std::string {
            if (pos + sizeof(uint32_t) > data.size()) return "";
            return readString();
        };

        // Version Control
        lic.min_browser_version = safeReadString();
        lic.max_browser_version = safeReadString();

        // Geographic/Compliance
        lic.allowed_regions = safeReadString();
        lic.export_control = safeReadString();

        // Usage Tracking
        if (pos + sizeof(uint32_t) <= data.size()) {
            lic.total_activations = readUint32();
        }
        lic.last_device_name = safeReadString();

        // Business Metadata
        lic.customer_id = safeReadString();
        lic.plan_id = safeReadString();
        lic.order_id = safeReadString();
        lic.invoice_id = safeReadString();
        lic.reseller_id = safeReadString();

        // Support/SLA
        if (pos + sizeof(uint8_t) <= data.size()) {
            lic.support_tier = static_cast<SupportTier>(readUint8());
        }
        if (pos + sizeof(int64_t) <= data.size()) {
            lic.support_expiry_timestamp = readInt64();
        }

        // Security
        lic.revocation_check_url = safeReadString();
        lic.issued_ip = safeReadString();

        // Maintenance
        if (pos + sizeof(uint8_t) <= data.size()) {
            lic.maintenance_included = readUint8() != 0;
        }
        if (pos + sizeof(int64_t) <= data.size()) {
            lic.maintenance_expiry_timestamp = readInt64();
        }
    }

    return lic;
}

// Helper for JSON string escaping
static std::string EscapeJSON(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string LicenseData::ToJSON() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"version\": " << version << ",\n";
    json << "  \"license_id\": \"" << EscapeJSON(license_id) << "\",\n";
    json << "  \"name\": \"" << EscapeJSON(name) << "\",\n";
    json << "  \"organization\": \"" << EscapeJSON(organization) << "\",\n";
    json << "  \"email\": \"" << EscapeJSON(email) << "\",\n";
    json << "  \"type\": " << static_cast<int>(type) << ",\n";
    json << "  \"type_name\": \"";
    switch (type) {
        case LicenseType::TRIAL: json << "trial"; break;
        case LicenseType::STARTER: json << "starter"; break;
        case LicenseType::BUSINESS: json << "business"; break;
        case LicenseType::ENTERPRISE: json << "enterprise"; break;
        case LicenseType::DEVELOPER: json << "developer"; break;
        case LicenseType::SUBSCRIPTION: json << "subscription"; break;
    }
    json << "\",\n";
    json << "  \"max_seats\": " << max_seats << ",\n";
    json << "  \"issue_timestamp\": " << issue_timestamp << ",\n";
    json << "  \"expiry_timestamp\": " << expiry_timestamp << ",\n";
    json << "  \"feature_flags\": " << feature_flags << ",\n";
    json << "  \"hardware_bound\": " << (hardware_bound ? "true" : "false") << ",\n";
    json << "  \"issuer\": \"" << EscapeJSON(issuer) << "\",\n";
    json << "  \"notes\": \"" << EscapeJSON(notes) << "\"";

    // Subscription fields
    if (type == LicenseType::SUBSCRIPTION) {
        json << ",\n";
        json << "  \"subscription\": {\n";
        json << "    \"activation_timestamp\": " << activation_timestamp << ",\n";
        json << "    \"last_check_timestamp\": " << last_check_timestamp << ",\n";
        json << "    \"next_check_timestamp\": " << next_check_timestamp << ",\n";
        json << "    \"grace_period_days\": " << grace_period_days << "\n";
        json << "  }";
    }

    // Version 2 extended metadata
    if (version >= 2) {
        json << ",\n";
        json << "  \"extended_metadata\": {\n";

        // Version Control
        json << "    \"min_browser_version\": \"" << EscapeJSON(min_browser_version) << "\",\n";
        json << "    \"max_browser_version\": \"" << EscapeJSON(max_browser_version) << "\",\n";

        // Geographic/Compliance
        json << "    \"allowed_regions\": \"" << EscapeJSON(allowed_regions) << "\",\n";
        json << "    \"export_control\": \"" << EscapeJSON(export_control) << "\",\n";

        // Usage Tracking
        json << "    \"total_activations\": " << total_activations << ",\n";
        json << "    \"last_device_name\": \"" << EscapeJSON(last_device_name) << "\",\n";

        // Business Metadata
        json << "    \"customer_id\": \"" << EscapeJSON(customer_id) << "\",\n";
        json << "    \"plan_id\": \"" << EscapeJSON(plan_id) << "\",\n";
        json << "    \"order_id\": \"" << EscapeJSON(order_id) << "\",\n";
        json << "    \"invoice_id\": \"" << EscapeJSON(invoice_id) << "\",\n";
        json << "    \"reseller_id\": \"" << EscapeJSON(reseller_id) << "\",\n";

        // Support/SLA
        json << "    \"support_tier\": " << static_cast<int>(support_tier) << ",\n";
        json << "    \"support_tier_name\": \"";
        switch (support_tier) {
            case SupportTier::NONE: json << "none"; break;
            case SupportTier::BASIC: json << "basic"; break;
            case SupportTier::STANDARD: json << "standard"; break;
            case SupportTier::PREMIUM: json << "premium"; break;
            case SupportTier::ENTERPRISE: json << "enterprise"; break;
        }
        json << "\",\n";
        json << "    \"support_expiry_timestamp\": " << support_expiry_timestamp << ",\n";

        // Security
        json << "    \"revocation_check_url\": \"" << EscapeJSON(revocation_check_url) << "\",\n";
        json << "    \"issued_ip\": \"" << EscapeJSON(issued_ip) << "\",\n";

        // Maintenance
        json << "    \"maintenance_included\": " << (maintenance_included ? "true" : "false") << ",\n";
        json << "    \"maintenance_expiry_timestamp\": " << maintenance_expiry_timestamp << "\n";

        json << "  }";
    }

    json << "\n}";
    return json.str();
}

bool LicenseFile::SaveToFile(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&flags), sizeof(flags));

    auto writeVector = [&file](const std::vector<uint8_t>& vec) {
        uint32_t size = static_cast<uint32_t>(vec.size());
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        if (size > 0) {
            file.write(reinterpret_cast<const char*>(vec.data()), size);
        }
    };

    writeVector(encrypted_data);
    writeVector(iv);
    writeVector(auth_tag);
    writeVector(signature);

    file.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));

    return file.good();
}

std::unique_ptr<LicenseFile> LicenseFile::LoadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return nullptr;

    auto lic = std::make_unique<LicenseFile>();

    file.read(reinterpret_cast<char*>(&lic->magic), sizeof(lic->magic));
    file.read(reinterpret_cast<char*>(&lic->version), sizeof(lic->version));
    file.read(reinterpret_cast<char*>(&lic->flags), sizeof(lic->flags));

    if (lic->magic != LICENSE_MAGIC) {
        return nullptr;
    }

    auto readVector = [&file]() -> std::vector<uint8_t> {
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (size > 10 * 1024 * 1024) {
            throw std::runtime_error("Invalid vector size");
        }
        std::vector<uint8_t> vec(size);
        if (size > 0) {
            file.read(reinterpret_cast<char*>(vec.data()), size);
        }
        return vec;
    };

    try {
        lic->encrypted_data = readVector();
        lic->iv = readVector();
        lic->auth_tag = readVector();
        lic->signature = readVector();
    } catch (...) {
        return nullptr;
    }

    file.read(reinterpret_cast<char*>(&lic->checksum), sizeof(lic->checksum));

    if (!file.good()) return nullptr;

    return lic;
}

// ============================================================================
// Crypto Implementation
// ============================================================================

#if defined(__APPLE__)

std::vector<uint8_t> Crypto::SHA256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(CC_SHA256_DIGEST_LENGTH);
    CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), hash.data());
    return hash;
}

std::vector<uint8_t> Crypto::RandomBytes(size_t length) {
    std::vector<uint8_t> bytes(length);
    CCRandomGenerateBytes(bytes.data(), length);
    return bytes;
}

// Helper to run shell command and capture output
static std::string RunCommand(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

bool Crypto::GenerateRSAKeyPair(std::string& private_key_pem, std::string& public_key_pem) {
    // Use openssl command-line tool for key generation (available on macOS)
    std::string temp_priv = "/tmp/owl_keygen_priv_" + std::to_string(getpid()) + ".pem";
    std::string temp_pub = "/tmp/owl_keygen_pub_" + std::to_string(getpid()) + ".pem";

    // Generate RSA 2048-bit private key
    std::string gen_cmd = "openssl genrsa -out " + temp_priv + " 2048 2>/dev/null";
    if (system(gen_cmd.c_str()) != 0) {
        return false;
    }

    // Extract public key
    std::string pub_cmd = "openssl rsa -in " + temp_priv + " -pubout -out " + temp_pub + " 2>/dev/null";
    if (system(pub_cmd.c_str()) != 0) {
        unlink(temp_priv.c_str());
        return false;
    }

    // Read private key
    std::ifstream priv_file(temp_priv);
    if (!priv_file.is_open()) {
        unlink(temp_priv.c_str());
        unlink(temp_pub.c_str());
        return false;
    }
    std::stringstream priv_ss;
    priv_ss << priv_file.rdbuf();
    private_key_pem = priv_ss.str();
    priv_file.close();

    // Read public key
    std::ifstream pub_file(temp_pub);
    if (!pub_file.is_open()) {
        unlink(temp_priv.c_str());
        unlink(temp_pub.c_str());
        return false;
    }
    std::stringstream pub_ss;
    pub_ss << pub_file.rdbuf();
    public_key_pem = pub_ss.str();
    pub_file.close();

    // Clean up temp files
    unlink(temp_priv.c_str());
    unlink(temp_pub.c_str());

    return !private_key_pem.empty() && !public_key_pem.empty();
}

bool Crypto::SignRSA(const std::vector<uint8_t>& data,
                      const std::string& private_key_pem,
                      std::vector<uint8_t>& signature) {
    // Use openssl command-line for signing
    std::string pid_str = std::to_string(getpid());
    std::string temp_key = "/tmp/owl_sign_key_" + pid_str + ".pem";
    std::string temp_data = "/tmp/owl_sign_data_" + pid_str + ".bin";
    std::string temp_sig = "/tmp/owl_sign_sig_" + pid_str + ".bin";

    // Write private key to temp file
    std::ofstream key_file(temp_key);
    if (!key_file.is_open()) return false;
    key_file << private_key_pem;
    key_file.close();

    // Write data to temp file
    std::ofstream data_file(temp_data, std::ios::binary);
    if (!data_file.is_open()) {
        unlink(temp_key.c_str());
        return false;
    }
    data_file.write(reinterpret_cast<const char*>(data.data()), data.size());
    data_file.close();

    // Sign with openssl
    std::string sign_cmd = "openssl dgst -sha256 -sign " + temp_key +
                           " -out " + temp_sig + " " + temp_data + " 2>/dev/null";
    int ret = system(sign_cmd.c_str());

    // Read signature
    bool success = false;
    if (ret == 0) {
        std::ifstream sig_file(temp_sig, std::ios::binary | std::ios::ate);
        if (sig_file.is_open()) {
            size_t size = sig_file.tellg();
            sig_file.seekg(0, std::ios::beg);
            signature.resize(size);
            sig_file.read(reinterpret_cast<char*>(signature.data()), size);
            success = sig_file.good();
            sig_file.close();
        }
    }

    // Cleanup
    unlink(temp_key.c_str());
    unlink(temp_data.c_str());
    unlink(temp_sig.c_str());

    return success && !signature.empty();
}

bool Crypto::VerifyRSA(const std::vector<uint8_t>& data,
                        const std::vector<uint8_t>& signature,
                        const std::string& public_key_pem) {
    // Use openssl command-line for verification
    std::string pid_str = std::to_string(getpid());
    std::string temp_key = "/tmp/owl_verify_key_" + pid_str + ".pem";
    std::string temp_data = "/tmp/owl_verify_data_" + pid_str + ".bin";
    std::string temp_sig = "/tmp/owl_verify_sig_" + pid_str + ".bin";

    // Write public key to temp file
    std::ofstream key_file(temp_key);
    if (!key_file.is_open()) return false;
    key_file << public_key_pem;
    key_file.close();

    // Write data to temp file
    std::ofstream data_file(temp_data, std::ios::binary);
    if (!data_file.is_open()) {
        unlink(temp_key.c_str());
        return false;
    }
    data_file.write(reinterpret_cast<const char*>(data.data()), data.size());
    data_file.close();

    // Write signature to temp file
    std::ofstream sig_file(temp_sig, std::ios::binary);
    if (!sig_file.is_open()) {
        unlink(temp_key.c_str());
        unlink(temp_data.c_str());
        return false;
    }
    sig_file.write(reinterpret_cast<const char*>(signature.data()), signature.size());
    sig_file.close();

    // Verify with openssl
    std::string verify_cmd = "openssl dgst -sha256 -verify " + temp_key +
                             " -signature " + temp_sig + " " + temp_data + " >/dev/null 2>&1";
    int ret = system(verify_cmd.c_str());

    // Cleanup
    unlink(temp_key.c_str());
    unlink(temp_data.c_str());
    unlink(temp_sig.c_str());

    return ret == 0;
}

// HMAC-SHA256 for authentication (since macOS doesn't have public GCM API)
static std::vector<uint8_t> ComputeHMAC(const std::vector<uint8_t>& key,
                                         const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hmac(CC_SHA256_DIGEST_LENGTH);
    CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data.data(), data.size(), hmac.data());
    return hmac;
}

bool Crypto::Encrypt(const std::vector<uint8_t>& plaintext,
                      const std::vector<uint8_t>& key,
                      std::vector<uint8_t>& ciphertext,
                      std::vector<uint8_t>& iv,
                      std::vector<uint8_t>& tag) {
    if (key.size() != 32) return false;

    // Use 16-byte IV for CBC mode
    iv = RandomBytes(16);

    // Calculate output size with PKCS7 padding
    size_t block_size = kCCBlockSizeAES128;
    size_t padded_size = ((plaintext.size() / block_size) + 1) * block_size;
    ciphertext.resize(padded_size);

    size_t out_len = 0;
    CCCryptorStatus status = CCCrypt(
        kCCEncrypt,
        kCCAlgorithmAES,
        kCCOptionPKCS7Padding,
        key.data(), key.size(),
        iv.data(),
        plaintext.data(), plaintext.size(),
        ciphertext.data(), ciphertext.size(),
        &out_len
    );

    if (status != kCCSuccess) return false;

    ciphertext.resize(out_len);

    // Compute HMAC over IV + ciphertext for authentication
    std::vector<uint8_t> hmac_data;
    hmac_data.insert(hmac_data.end(), iv.begin(), iv.end());
    hmac_data.insert(hmac_data.end(), ciphertext.begin(), ciphertext.end());
    tag = ComputeHMAC(key, hmac_data);

    return true;
}

bool Crypto::Decrypt(const std::vector<uint8_t>& ciphertext,
                      const std::vector<uint8_t>& key,
                      const std::vector<uint8_t>& iv,
                      const std::vector<uint8_t>& tag,
                      std::vector<uint8_t>& plaintext) {
    if (key.size() != 32 || iv.size() != 16 || tag.size() != CC_SHA256_DIGEST_LENGTH) return false;

    // Verify HMAC first
    std::vector<uint8_t> hmac_data;
    hmac_data.insert(hmac_data.end(), iv.begin(), iv.end());
    hmac_data.insert(hmac_data.end(), ciphertext.begin(), ciphertext.end());
    std::vector<uint8_t> computed_tag = ComputeHMAC(key, hmac_data);

    // Constant-time comparison
    if (computed_tag.size() != tag.size()) return false;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < tag.size(); ++i) {
        diff |= computed_tag[i] ^ tag[i];
    }
    if (diff != 0) return false;

    // Decrypt
    plaintext.resize(ciphertext.size());
    size_t out_len = 0;

    CCCryptorStatus status = CCCrypt(
        kCCDecrypt,
        kCCAlgorithmAES,
        kCCOptionPKCS7Padding,
        key.data(), key.size(),
        iv.data(),
        ciphertext.data(), ciphertext.size(),
        plaintext.data(), plaintext.size(),
        &out_len
    );

    if (status != kCCSuccess) return false;

    plaintext.resize(out_len);
    return true;
}

#else  // Linux with OpenSSL

std::vector<uint8_t> Crypto::SHA256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    ::SHA256(data.data(), data.size(), hash.data());
    return hash;
}

std::vector<uint8_t> Crypto::RandomBytes(size_t length) {
    std::vector<uint8_t> bytes(length);
    RAND_bytes(bytes.data(), static_cast<int>(length));
    return bytes;
}

bool Crypto::GenerateRSAKeyPair(std::string& private_key_pem, std::string& public_key_pem) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) return false;

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Export private key
    BIO* bio_priv = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio_priv, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* priv_data;
    long priv_len = BIO_get_mem_data(bio_priv, &priv_data);
    private_key_pem.assign(priv_data, priv_len);
    BIO_free(bio_priv);

    // Export public key
    BIO* bio_pub = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio_pub, pkey);
    char* pub_data;
    long pub_len = BIO_get_mem_data(bio_pub, &pub_data);
    public_key_pem.assign(pub_data, pub_len);
    BIO_free(bio_pub);

    EVP_PKEY_free(pkey);
    return true;
}

bool Crypto::SignRSA(const std::vector<uint8_t>& data,
                      const std::string& private_key_pem,
                      std::vector<uint8_t>& signature) {
    BIO* bio = BIO_new_mem_buf(private_key_pem.data(), static_cast<int>(private_key_pem.size()));
    if (!bio) return false;

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool result = false;
    size_t sig_len = 0;

    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestSignUpdate(ctx, data.data(), data.size()) == 1) {
            if (EVP_DigestSignFinal(ctx, nullptr, &sig_len) == 1) {
                signature.resize(sig_len);
                if (EVP_DigestSignFinal(ctx, signature.data(), &sig_len) == 1) {
                    signature.resize(sig_len);
                    result = true;
                }
            }
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}

bool Crypto::VerifyRSA(const std::vector<uint8_t>& data,
                        const std::vector<uint8_t>& signature,
                        const std::string& public_key_pem) {
    BIO* bio = BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size()));
    if (!bio) return false;

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool result = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) == 1) {
            result = EVP_DigestVerifyFinal(ctx, signature.data(), signature.size()) == 1;
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}

// Linux implementation uses AES-256-CBC with HMAC-SHA256 for cross-platform
// compatibility with macOS (which uses CommonCrypto with the same scheme)
bool Crypto::Encrypt(const std::vector<uint8_t>& plaintext,
                      const std::vector<uint8_t>& key,
                      std::vector<uint8_t>& ciphertext,
                      std::vector<uint8_t>& iv,
                      std::vector<uint8_t>& tag) {
    if (key.size() != 32) return false;

    // Generate 16-byte IV for CBC mode (matching macOS)
    iv = RandomBytes(16);

    // Allocate space for ciphertext (may be larger due to padding)
    ciphertext.resize(plaintext.size() + EVP_MAX_BLOCK_LENGTH);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool result = false;
    int len = 0;
    int final_len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) == 1) {
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(),
                               static_cast<int>(plaintext.size())) == 1) {
            if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &final_len) == 1) {
                ciphertext.resize(len + final_len);

                // Compute HMAC-SHA256 over IV + ciphertext (matching macOS)
                tag.resize(SHA256_DIGEST_LENGTH);
                std::vector<uint8_t> hmac_data;
                hmac_data.insert(hmac_data.end(), iv.begin(), iv.end());
                hmac_data.insert(hmac_data.end(), ciphertext.begin(), ciphertext.end());

                unsigned int hmac_len = SHA256_DIGEST_LENGTH;
                if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                         hmac_data.data(), hmac_data.size(),
                         tag.data(), &hmac_len) != nullptr) {
                    result = true;
                }
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return result;
}

bool Crypto::Decrypt(const std::vector<uint8_t>& ciphertext,
                      const std::vector<uint8_t>& key,
                      const std::vector<uint8_t>& iv,
                      const std::vector<uint8_t>& tag,
                      std::vector<uint8_t>& plaintext) {
    // Accept 16-byte IV (CBC from macOS) and verify tag is SHA256 (32 bytes)
    if (key.size() != 32 || iv.size() != 16 || tag.size() != SHA256_DIGEST_LENGTH) return false;

    // Verify HMAC first (computed over IV + ciphertext) - matching macOS
    std::vector<uint8_t> computed_tag(SHA256_DIGEST_LENGTH);
    std::vector<uint8_t> hmac_data;
    hmac_data.insert(hmac_data.end(), iv.begin(), iv.end());
    hmac_data.insert(hmac_data.end(), ciphertext.begin(), ciphertext.end());

    unsigned int hmac_len = SHA256_DIGEST_LENGTH;
    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
             hmac_data.data(), hmac_data.size(),
             computed_tag.data(), &hmac_len) == nullptr) {
        return false;
    }

    // Constant-time comparison of full tag
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        diff |= computed_tag[i] ^ tag[i];
    }
    if (diff != 0) return false;

    // Decrypt using AES-256-CBC
    plaintext.resize(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool result = false;
    int len = 0;
    int final_len = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) == 1) {
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(),
                               static_cast<int>(ciphertext.size())) == 1) {
            if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &final_len) == 1) {
                plaintext.resize(len + final_len);
                result = true;
            }
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return result;
}

#endif

std::string Crypto::SHA256Hex(const std::string& data) {
    std::vector<uint8_t> hash = SHA256(std::vector<uint8_t>(data.begin(), data.end()));
    std::ostringstream ss;
    for (uint8_t byte : hash) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string Crypto::GenerateUUID() {
    std::vector<uint8_t> bytes = RandomBytes(16);
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream ss;
    for (size_t i = 0; i < 16; ++i) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) ss << '-';
    }
    return ss.str();
}

std::vector<uint8_t> Crypto::DeriveAESKey(const std::string& public_key_pem) {
    return SHA256(std::vector<uint8_t>(public_key_pem.begin(), public_key_pem.end()));
}

// ============================================================================
// License Generator
// ============================================================================

class LicenseGenerator {
public:
    LicenseGenerator() {
        LoadKeys();
    }

    bool GenerateLicense(const LicenseData& data, const std::string& output_path) {
        if (private_key_.empty()) {
            std::cerr << "Error: No private key loaded. Run 'keygen' first." << std::endl;
            return false;
        }

        // Serialize license data
        std::vector<uint8_t> plaintext = data.Serialize();

        // Derive AES key from public key
        std::vector<uint8_t> aes_key = Crypto::DeriveAESKey(public_key_);

        // Encrypt license data
        LicenseFile file;
        if (!Crypto::Encrypt(plaintext, aes_key, file.encrypted_data, file.iv, file.auth_tag)) {
            std::cerr << "Error: Failed to encrypt license data" << std::endl;
            return false;
        }

        // Sign the encrypted data
        if (!Crypto::SignRSA(file.encrypted_data, private_key_, file.signature)) {
            std::cerr << "Error: Failed to sign license data" << std::endl;
            return false;
        }

        // Calculate checksum
        std::vector<uint8_t> all_data;
        all_data.insert(all_data.end(), file.encrypted_data.begin(), file.encrypted_data.end());
        all_data.insert(all_data.end(), file.iv.begin(), file.iv.end());
        all_data.insert(all_data.end(), file.auth_tag.begin(), file.auth_tag.end());
        all_data.insert(all_data.end(), file.signature.begin(), file.signature.end());
        std::vector<uint8_t> checksum_hash = Crypto::SHA256(all_data);
        file.checksum = *reinterpret_cast<uint32_t*>(checksum_hash.data());

        // Save to file
        if (!file.SaveToFile(output_path)) {
            std::cerr << "Error: Failed to save license file" << std::endl;
            return false;
        }

        return true;
    }

    bool VerifyLicense(const std::string& license_path) {
        auto file = LicenseFile::LoadFromFile(license_path);
        if (!file) {
            std::cerr << "Error: Failed to load license file" << std::endl;
            return false;
        }

        // Verify signature
        if (!Crypto::VerifyRSA(file->encrypted_data, file->signature, public_key_)) {
            std::cerr << "Error: Invalid signature" << std::endl;
            return false;
        }

        // Decrypt and display
        std::vector<uint8_t> aes_key = Crypto::DeriveAESKey(public_key_);
        std::vector<uint8_t> plaintext;
        if (!Crypto::Decrypt(file->encrypted_data, aes_key, file->iv, file->auth_tag, plaintext)) {
            std::cerr << "Error: Failed to decrypt license data" << std::endl;
            return false;
        }

        try {
            LicenseData data = LicenseData::Deserialize(plaintext);
            std::cout << "License verified successfully!" << std::endl;
            std::cout << data.ToJSON() << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return false;
        }
    }

    void ShowInfo(const std::string& license_path) {
        auto file = LicenseFile::LoadFromFile(license_path);
        if (!file) {
            std::cerr << "Error: Failed to load license file" << std::endl;
            return;
        }

        std::cout << "License File: " << license_path << std::endl;
        std::cout << "Magic: 0x" << std::hex << file->magic << std::dec << std::endl;
        std::cout << "Version: " << file->version << std::endl;
        std::cout << "Encrypted Data Size: " << file->encrypted_data.size() << " bytes" << std::endl;
        std::cout << "Signature Size: " << file->signature.size() << " bytes" << std::endl;

        // Try to decrypt and show info
        std::vector<uint8_t> aes_key = Crypto::DeriveAESKey(public_key_);
        std::vector<uint8_t> plaintext;
        if (Crypto::Decrypt(file->encrypted_data, aes_key, file->iv, file->auth_tag, plaintext)) {
            try {
                LicenseData data = LicenseData::Deserialize(plaintext);
                std::cout << "\nLicense Data:\n" << data.ToJSON() << std::endl;
            } catch (...) {
                std::cout << "\nFailed to parse license data" << std::endl;
            }
        }
    }

    bool GenerateKeyPair() {
        std::string priv, pub;
        if (!Crypto::GenerateRSAKeyPair(priv, pub)) {
            std::cerr << "Error: Failed to generate RSA key pair" << std::endl;
            return false;
        }

        // Save keys
        std::string key_dir = GetKeyDirectory();
        std::string priv_path = key_dir + "/owl_license.key";
        std::string pub_path = key_dir + "/owl_license.pub";

        std::ofstream priv_file(priv_path);
        priv_file << priv;
        priv_file.close();

        std::ofstream pub_file(pub_path);
        pub_file << pub;
        pub_file.close();

        std::cout << "Keys generated successfully!" << std::endl;
        std::cout << "Private key: " << priv_path << std::endl;
        std::cout << "Public key: " << pub_path << std::endl;
        std::cout << "\nIMPORTANT: Keep the private key secure!" << std::endl;
        std::cout << "The public key should be embedded in the browser source." << std::endl;

        private_key_ = priv;
        public_key_ = pub;

        return true;
    }

private:
    std::string private_key_;
    std::string public_key_;

    std::string GetKeyDirectory() {
        const char* home = getenv("HOME");
        if (!home) return ".";
        return std::string(home) + "/.owl_license";
    }

    // Helper to trim trailing whitespace/newlines
    static std::string TrimTrailing(const std::string& s) {
        size_t end = s.find_last_not_of(" \t\n\r");
        return (end == std::string::npos) ? "" : s.substr(0, end + 1);
    }

    void LoadKeys() {
        std::string key_dir = GetKeyDirectory();

        // Try to load private key
        std::ifstream priv_file(key_dir + "/owl_license.key");
        if (priv_file.is_open()) {
            std::stringstream buffer;
            buffer << priv_file.rdbuf();
            private_key_ = TrimTrailing(buffer.str());
        }

        // Try to load public key
        std::ifstream pub_file(key_dir + "/owl_license.pub");
        if (pub_file.is_open()) {
            std::stringstream buffer;
            buffer << pub_file.rdbuf();
            public_key_ = TrimTrailing(buffer.str());
        }

        // If no public key, use embedded default
        if (public_key_.empty()) {
            public_key_ = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvJ8kLSOGR3hMvXQzN8Yh
9LmT3k5x2wD4PfHj6Y2K8cRnM7F1pBq5jNkL2vS4dXoH9gWe1rK6YmN8t0Jx3pLq
R5sT1aV2wU6cD4mE8fY7iH1jK3lM4nO5pQ6rS7tU8vW9xY0zA1bC2dE3fG4hI5jK
6lM7nO8pQ9rS0tU1vW2xY3zA4bC5dE6fG7hI8jK9lM0nO1pQ2rS3tU4vW5xY6zA7
bC8dE9fG0hI1jK2lM3nO4pQ5rS6tU7vW8xY9zA0bC1dE2fG3hI4jK5lM6nO7pQ8r
S9tU0vW1xY2zA3bC4dE5fG6hI7jK8lM9nO0pQ1rS2tU3vW4xY5zA6bC7dE8fG9hI
0wIDAQAB
-----END PUBLIC KEY-----)";
        }
    }
};

// ============================================================================
// CLI
// ============================================================================

void PrintUsage(const char* program) {
    std::cout << "Owl Browser License Generator (Version 2)\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << program << " keygen\n";
    std::cout << "    Generate a new RSA-2048 key pair\n\n";
    std::cout << "  " << program << " generate [options] --output <file>\n";
    std::cout << "    Generate a new license file\n\n";
    std::cout << "    Required Options:\n";
    std::cout << "      --name <name>              Licensee name\n";
    std::cout << "      --email <email>            Contact email\n";
    std::cout << "      --output <file>            Output license file path\n\n";
    std::cout << "    License Options:\n";
    std::cout << "      --org <organization>       Organization name\n";
    std::cout << "      --type <type>              License type: trial, starter, business,\n";
    std::cout << "                                 enterprise, developer, subscription (default: trial)\n";
    std::cout << "      --license-id <uuid>        Use existing license ID (for reissuing)\n";
    std::cout << "      --seats <n>                Number of allowed devices (default: 1)\n";
    std::cout << "      --expiry <days>            Days until expiry (0 = perpetual)\n";
    std::cout << "      --hardware-bound           Bind license to current hardware\n";
    std::cout << "      --hardware-id <id>         Specific hardware fingerprint to bind to\n";
    std::cout << "      --grace-period <days>      Days to allow offline use (subscription only, default: 7)\n";
    std::cout << "      --notes <text>             Internal notes\n\n";
    std::cout << "    Version Control:\n";
    std::cout << "      --min-version <version>    Minimum browser version required (e.g., \"1.0.0\")\n";
    std::cout << "      --max-version <version>    Maximum browser version allowed\n\n";
    std::cout << "    Geographic/Compliance:\n";
    std::cout << "      --regions <codes>          Allowed region codes, comma-separated (e.g., \"US,EU,CA\")\n";
    std::cout << "      --export-control <class>   Export control classification\n\n";
    std::cout << "    Business Metadata:\n";
    std::cout << "      --customer-id <id>         Customer ID in server database\n";
    std::cout << "      --plan-id <id>             Billing plan ID\n";
    std::cout << "      --order-id <id>            Order/purchase reference\n";
    std::cout << "      --invoice-id <id>          Invoice reference\n";
    std::cout << "      --reseller-id <id>         Reseller/partner ID\n\n";
    std::cout << "    Support/SLA:\n";
    std::cout << "      --support-tier <tier>      Support tier: none, basic, standard, premium, enterprise\n";
    std::cout << "      --support-expiry <days>    Days until support expires\n\n";
    std::cout << "    Maintenance:\n";
    std::cout << "      --maintenance              Include maintenance/updates\n";
    std::cout << "      --maintenance-expiry <days> Days until maintenance expires\n\n";
    std::cout << "  " << program << " verify <license-file>\n";
    std::cout << "    Verify a license file signature\n\n";
    std::cout << "  " << program << " info <license-file>\n";
    std::cout << "    Show license file information\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program << " generate --name \"John Doe\" --email john@example.com \\\n";
    std::cout << "                  --type business --seats 10 --expiry 365 \\\n";
    std::cout << "                  --customer-id cust_123 --plan-id plan_business \\\n";
    std::cout << "                  --output license.olic\n\n";
    std::cout << "  " << program << " generate --name \"Acme Corp\" --org \"Acme Corporation\" \\\n";
    std::cout << "                  --email admin@acme.com --type enterprise --seats 100 \\\n";
    std::cout << "                  --min-version \"1.0.0\" --regions \"US,CA\" \\\n";
    std::cout << "                  --support-tier enterprise --support-expiry 365 \\\n";
    std::cout << "                  --maintenance --maintenance-expiry 365 \\\n";
    std::cout << "                  --output acme_license.olic\n\n";
}

LicenseType ParseLicenseType(const std::string& type) {
    if (type == "trial") return LicenseType::TRIAL;
    if (type == "starter") return LicenseType::STARTER;
    if (type == "business") return LicenseType::BUSINESS;
    if (type == "enterprise") return LicenseType::ENTERPRISE;
    if (type == "developer") return LicenseType::DEVELOPER;
    if (type == "subscription") return LicenseType::SUBSCRIPTION;
    return LicenseType::TRIAL;
}

SupportTier ParseSupportTier(const std::string& tier) {
    if (tier == "none") return SupportTier::NONE;
    if (tier == "basic") return SupportTier::BASIC;
    if (tier == "standard") return SupportTier::STANDARD;
    if (tier == "premium") return SupportTier::PREMIUM;
    if (tier == "enterprise") return SupportTier::ENTERPRISE;
    return SupportTier::NONE;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    LicenseGenerator generator;

    if (command == "keygen") {
        return generator.GenerateKeyPair() ? 0 : 1;
    }
    else if (command == "generate") {
        LicenseData data;
        std::string output_path;

        // Parse arguments
        int support_expiry_days = 0;
        int maintenance_expiry_days = 0;

        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            // Basic license options
            if (arg == "--name" && i + 1 < argc) {
                data.name = argv[++i];
            } else if (arg == "--org" && i + 1 < argc) {
                data.organization = argv[++i];
            } else if (arg == "--email" && i + 1 < argc) {
                data.email = argv[++i];
            } else if (arg == "--type" && i + 1 < argc) {
                data.type = ParseLicenseType(argv[++i]);
            } else if (arg == "--seats" && i + 1 < argc) {
                data.max_seats = std::stoi(argv[++i]);
            } else if (arg == "--expiry" && i + 1 < argc) {
                int days = std::stoi(argv[++i]);
                if (days > 0) {
                    auto now = std::chrono::system_clock::now();
                    auto epoch = now.time_since_epoch();
                    int64_t now_secs = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
                    data.expiry_timestamp = now_secs + (days * 86400);
                }
            } else if (arg == "--hardware-bound") {
                data.hardware_bound = true;
            } else if (arg == "--hardware-id" && i + 1 < argc) {
                data.hardware_bound = true;
                data.hardware_fingerprint = argv[++i];
            } else if (arg == "--grace-period" && i + 1 < argc) {
                data.grace_period_days = static_cast<uint32_t>(std::stoi(argv[++i]));
            } else if (arg == "--notes" && i + 1 < argc) {
                data.notes = argv[++i];
            } else if (arg == "--output" && i + 1 < argc) {
                output_path = argv[++i];
            } else if (arg == "--license-id" && i + 1 < argc) {
                data.license_id = argv[++i];  // Use existing license ID (for reissuing)
            }
            // Version control
            else if (arg == "--min-version" && i + 1 < argc) {
                data.min_browser_version = argv[++i];
            } else if (arg == "--max-version" && i + 1 < argc) {
                data.max_browser_version = argv[++i];
            }
            // Geographic/Compliance
            else if (arg == "--regions" && i + 1 < argc) {
                data.allowed_regions = argv[++i];
            } else if (arg == "--export-control" && i + 1 < argc) {
                data.export_control = argv[++i];
            }
            // Business metadata
            else if (arg == "--customer-id" && i + 1 < argc) {
                data.customer_id = argv[++i];
            } else if (arg == "--plan-id" && i + 1 < argc) {
                data.plan_id = argv[++i];
            } else if (arg == "--order-id" && i + 1 < argc) {
                data.order_id = argv[++i];
            } else if (arg == "--invoice-id" && i + 1 < argc) {
                data.invoice_id = argv[++i];
            } else if (arg == "--reseller-id" && i + 1 < argc) {
                data.reseller_id = argv[++i];
            }
            // Support/SLA
            else if (arg == "--support-tier" && i + 1 < argc) {
                data.support_tier = ParseSupportTier(argv[++i]);
            } else if (arg == "--support-expiry" && i + 1 < argc) {
                support_expiry_days = std::stoi(argv[++i]);
            }
            // Maintenance
            else if (arg == "--maintenance") {
                data.maintenance_included = true;
            } else if (arg == "--maintenance-expiry" && i + 1 < argc) {
                maintenance_expiry_days = std::stoi(argv[++i]);
                data.maintenance_included = true;
            }
        }

        // Validate required fields
        if (data.name.empty() || data.email.empty() || output_path.empty()) {
            std::cerr << "Error: --name, --email, and --output are required\n";
            PrintUsage(argv[0]);
            return 1;
        }

        // Subscription-specific validation
        if (data.type == LicenseType::SUBSCRIPTION) {
            if (data.expiry_timestamp == 0) {
                // Default to 1 year for subscription licenses
                auto now = std::chrono::system_clock::now();
                auto epoch = now.time_since_epoch();
                int64_t now_secs = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
                data.expiry_timestamp = now_secs + (365 * 86400);
                std::cout << "Note: Subscription license defaulting to 1 year expiry\n";
            }
            if (data.grace_period_days == 0) {
                data.grace_period_days = 7;  // Default 7 days grace period
            }
        }

        // Generate license ID (or use provided one for reissuing) and set timestamps
        if (data.license_id.empty()) {
            data.license_id = Crypto::GenerateUUID();
        }
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        int64_t now_secs = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        data.issue_timestamp = now_secs;
        data.issuer = "Owl License Generator v2";

        // Set support expiry timestamp if specified
        if (support_expiry_days > 0) {
            data.support_expiry_timestamp = now_secs + (support_expiry_days * 86400LL);
        }

        // Set maintenance expiry timestamp if specified
        if (maintenance_expiry_days > 0) {
            data.maintenance_expiry_timestamp = now_secs + (maintenance_expiry_days * 86400LL);
        } else if (data.maintenance_included && data.expiry_timestamp > 0) {
            // Default maintenance to same as license expiry
            data.maintenance_expiry_timestamp = data.expiry_timestamp;
        }

        // Generate license
        if (generator.GenerateLicense(data, output_path)) {
            std::cout << "License generated successfully: " << output_path << std::endl;
            std::cout << "License ID: " << data.license_id << std::endl;
            std::cout << "Version: " << data.version << std::endl;

            if (data.type == LicenseType::SUBSCRIPTION) {
                std::cout << "\n*** SUBSCRIPTION LICENSE ***" << std::endl;
                std::cout << "IMPORTANT: Register this license_id in the license server database!" << std::endl;
                std::cout << "The license will not work until it is registered as active." << std::endl;
                std::cout << "Grace period: " << data.grace_period_days << " days" << std::endl;
            }

            // Show extended metadata summary
            if (!data.customer_id.empty()) {
                std::cout << "Customer ID: " << data.customer_id << std::endl;
            }
            if (!data.min_browser_version.empty()) {
                std::cout << "Min Browser Version: " << data.min_browser_version << std::endl;
            }
            if (!data.allowed_regions.empty()) {
                std::cout << "Allowed Regions: " << data.allowed_regions << std::endl;
            }
            if (data.support_tier != SupportTier::NONE) {
                std::cout << "Support Tier: ";
                switch (data.support_tier) {
                    case SupportTier::BASIC: std::cout << "Basic"; break;
                    case SupportTier::STANDARD: std::cout << "Standard"; break;
                    case SupportTier::PREMIUM: std::cout << "Premium"; break;
                    case SupportTier::ENTERPRISE: std::cout << "Enterprise"; break;
                    default: std::cout << "None"; break;
                }
                std::cout << std::endl;
            }
            if (data.maintenance_included) {
                std::cout << "Maintenance: Included" << std::endl;
            }

            return 0;
        }
        return 1;
    }
    else if (command == "verify") {
        if (argc < 3) {
            std::cerr << "Error: License file path required\n";
            return 1;
        }
        return generator.VerifyLicense(argv[2]) ? 0 : 1;
    }
    else if (command == "info") {
        if (argc < 3) {
            std::cerr << "Error: License file path required\n";
            return 1;
        }
        generator.ShowInfo(argv[2]);
        return 0;
    }
    else {
        PrintUsage(argv[0]);
        return 1;
    }
}
