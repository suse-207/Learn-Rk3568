#pragma once

#include <cstdint>
#include <string>

namespace ota {

/// UCM error codes, modeled after ara::ucm's UCMErrorDomainErrc.
/// The enum value is used directly as the SOME-IP wire status code.
enum class UcmErrc : std::uint32_t {
    kSuccess = 0,
    kInvalidTransferId = 1,
    kInvalidPackageManifest = 2,
    kAuthenticationFailed = 3,
    kInsufficientMemory = 4,
    kOperationNotPermitted = 5,
    kServiceBusy = 6,
    kIncompatibleDelta = 7,
    kProcessedSoftwarePackageInconsistent = 8,
    kProcessSwPackageCancelled = 9,
    kSoftwareClusterMissing = 10,
    kIncompatiblePackageVersion = 11,
    kMissingDependencies = 12,
    kOldVersion = 13,
    kPackageInconsistent = 14,
    kIncorrectBlock = 15,
    kIncorrectBlockSize = 16,
    kBlockInconsistent = 17,
    kIncorrectSize = 18,
    kInsufficientData = 19,
    kNothingToRollback = 20,
    kNotAbleToRollback = 21,
    kNothingToRevert = 22,
    kNotAbleToRevertPackages = 23,
};

/// Wire/query data types (serialized over SOME-IP).
struct SwPackageInfo {
    std::string name;
    std::string version;
};

struct SwClusterInfo {
    std::string name;
    std::string version;
    std::string state;
};

struct HistoryEntry {
    std::string from;
    std::string to;
    std::string result;
};

struct ProgressInfo {
    std::string state;
    int percent{0};
    std::string detail;
};

}  // namespace ota
