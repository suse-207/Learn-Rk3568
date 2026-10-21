#include "ota/linuxab_slot_manager.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "common/log/log.h"

namespace ota {
namespace {

constexpr std::uint32_t kRkImageTag = 0x46414B52u;   // "RKAF"
constexpr std::uint32_t kRkFwMagic = 0x57464B52u;    // "RKFW" (new packing)
constexpr std::size_t kPartNameLen = 32;
constexpr std::size_t kPartFileLen = 64;
constexpr std::size_t kMaxItems = 16;

struct RkImageItem {
    char name[kPartNameLen];
    char file[kPartFileLen];
    std::uint32_t offset;
    std::uint32_t flash_offset;
    std::uint32_t usespace;
    std::uint32_t size;
};

struct RkImageHeader {
    std::uint32_t tag;
    std::uint32_t size;
    char machine_model[64];
    char manufacturer[60];
    std::uint32_t version;
    std::int32_t item_count;
    RkImageItem item[kMaxItems];
};

std::uint32_t ReadU32Le(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool RunCommand(const char* cmd) {
    if (cmd == nullptr) {
        return false;
    }
    const int ret = std::system(cmd);
    return ret == 0;
}

}  // namespace

LinuxAbSlotManager::LinuxAbSlotManager() {
    if (!detect_current_slot(current_)) {
        current_ = "A";
    }
}

std::string LinuxAbSlotManager::current_slot() const {
    return current_;
}

std::string LinuxAbSlotManager::inactive_slot() const {
    return current_ == "A" ? "B" : "A";
}

bool LinuxAbSlotManager::detect_current_slot(std::string& slot) const {
    std::ifstream cmdline("/proc/cmdline");
    if (!cmdline) {
        return false;
    }
    std::string line;
    std::getline(cmdline, line);
    cmdline.close();

    constexpr const char* kKey = "androidboot.slot_suffix=";
    const std::size_t pos = line.find(kKey);
    if (pos == std::string::npos) {
        return false;
    }
    const std::string suffix = line.substr(pos + std::strlen(kKey), 2);
    if (suffix == "_a") {
        slot = "A";
        return true;
    }
    if (suffix == "_b") {
        slot = "B";
        return true;
    }
    return false;
}

bool LinuxAbSlotManager::write_image_to_inactive(const std::string& image_path,
                                                 const std::string& /*version*/) {
    const std::string target = inactive_slot();
    const char slot_char = target == "A" ? 'a' : 'b';

    int fd = ::open(image_path.c_str(), O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("open OTA image failed: %s", image_path.c_str());
        return false;
    }

    // The "new packing" firmware has a 512-byte RKFW header. In that case the
    // real RKIMAGE header starts at gFwOffset, and every item offset must be
    // shifted by that amount.
    unsigned char head[512];
    std::uint32_t gfw_offset = 0;
    ssize_t n = ::read(fd, head, sizeof(head));
    if (n != static_cast<ssize_t>(sizeof(head))) {
        LOG_ERROR("read OTA image head failed");
        ::close(fd);
        return false;
    }
    if (ReadU32Le(head) == kRkFwMagic) {
        gfw_offset = ReadU32Le(head + 0x21);
    }

    if (::lseek(fd, static_cast<off_t>(gfw_offset), SEEK_SET) < 0) {
        LOG_ERROR("seek RKIMAGE header failed");
        ::close(fd);
        return false;
    }

    RkImageHeader hdr{};
    n = ::read(fd, &hdr, sizeof(hdr));
    if (n != static_cast<ssize_t>(sizeof(hdr)) || hdr.tag != kRkImageTag ||
        hdr.item_count <= 0 || hdr.item_count > static_cast<std::int32_t>(kMaxItems)) {
        LOG_ERROR("invalid RKIMAGE header");
        ::close(fd);
        return false;
    }

    bool ok = true;
    std::string error;
    for (std::int32_t i = 0; i < hdr.item_count && ok; ++i) {
        RkImageItem& item = hdr.item[i];
        if (gfw_offset != 0) {
            item.offset += gfw_offset;
        }

        std::string device;
        if (std::strcmp(item.name, "boot") == 0) {
            device = std::string("/dev/block/by-name/boot_") + slot_char;
        } else if (std::strcmp(item.name, "rootfs") == 0) {
            device = std::string("/dev/block/by-name/system_") + slot_char;
        } else {
            continue;  // package-file / bootloader / parameter are not slot images.
        }

        ok = write_item(image_path, item.name, item.offset, item.size, device, error);
        if (!ok) {
            LOG_ERROR("write %s to %s failed: %s", item.name, device.c_str(), error.c_str());
        }
    }

    ::close(fd);
    return ok;
}

bool LinuxAbSlotManager::write_item(const std::string& image_path,
                                    const std::string& item_name,
                                    std::uint32_t item_offset,
                                    std::uint32_t item_size,
                                    const std::string& device,
                                    std::string& error) const {
    int src = ::open(image_path.c_str(), O_RDONLY);
    if (src < 0) {
        error = "open image failed";
        return false;
    }
    if (::lseek(src, static_cast<off_t>(item_offset), SEEK_SET) < 0) {
        error = "seek item failed";
        ::close(src);
        return false;
    }

    int dst = ::open(device.c_str(), O_WRONLY);
    if (dst < 0) {
        error = "open partition failed";
        ::close(src);
        return false;
    }

    std::vector<unsigned char> buf(1024 * 1024);
    std::uint32_t remaining = item_size;
    bool ok = true;
    while (remaining > 0) {
        const std::size_t want = std::min<std::size_t>(buf.size(), remaining);
        const ssize_t rn = ::read(src, buf.data(), want);
        if (rn <= 0) {
            error = "read image item failed";
            ok = false;
            break;
        }
        const ssize_t wn = ::write(dst, buf.data(), static_cast<std::size_t>(rn));
        if (wn != rn) {
            error = "write partition failed";
            ok = false;
            break;
        }
        remaining -= static_cast<std::uint32_t>(rn);
    }

    ::fsync(dst);
    ::close(dst);
    ::close(src);
    if (ok) {
        LOG_INFO("wrote %s to %s (%u bytes)", item_name.c_str(), device.c_str(), item_size);
    }
    return ok;
}

bool LinuxAbSlotManager::activate_inactive() {
    return RunCommand("rkboot_control other");
}

bool LinuxAbSlotManager::rollback_to_last_good() {
    return RunCommand("rkboot_control other");
}

bool LinuxAbSlotManager::is_inactive_valid() const {
    const char slot_char = inactive_slot() == "A" ? 'a' : 'b';
    const std::string boot = std::string("/dev/block/by-name/boot_") + slot_char;
    const std::string system = std::string("/dev/block/by-name/system_") + slot_char;
    return ::access(boot.c_str(), F_OK) == 0 && ::access(system.c_str(), F_OK) == 0;
}

}  // namespace ota
