#include "uds/dispatcher.h"

#include <utility>

#include "common/log/log.h"

namespace uds {

UdsDispatcher::UdsDispatcher() {
    handlers_[kSidDiagSessionControl] =
        [this](int fd, const UdsRequest& r) { return handle_session_control(fd, r); };
    handlers_[kSidReadDataByIdentifier] =
        [this](int fd, const UdsRequest& r) { return handle_read_data(fd, r); };
    handlers_[kSidWriteDataByIdentifier] =
        [this](int fd, const UdsRequest& r) { return handle_write_data(fd, r); };
    handlers_[kSidRoutineControl] =
        [this](int fd, const UdsRequest& r) { return handle_routine_control(fd, r); };
    handlers_[kSidRequestDownload] =
        [this](int fd, const UdsRequest& r) { return handle_request_download(fd, r); };
    handlers_[kSidTransferData] =
        [this](int fd, const UdsRequest& r) { return handle_transfer_data(fd, r); };
    handlers_[kSidRequestTransferExit] =
        [this](int fd, const UdsRequest& r) { return handle_request_transfer_exit(fd, r); };
}

std::vector<std::uint8_t> UdsDispatcher::Handle(int fd, const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) {
        return {};
    }

    UdsRequest req;
    req.sid = data[0];
    req.params.assign(data + 1, data + len);

    UdsResponse resp;
    const auto it = handlers_.find(req.sid);
    if (it == handlers_.end()) {
        resp.negative = true;
        resp.nrc = kNrcServiceNotSupported;
    } else {
        resp = it->second(fd, req);
    }

    std::vector<std::uint8_t> out;
    if (resp.negative) {
        out = {0x7F, req.sid, resp.nrc};
    } else {
        out.push_back(static_cast<std::uint8_t>(req.sid + 0x40));
        out.insert(out.end(), resp.data.begin(), resp.data.end());
    }
    return out;
}

UdsResponse UdsDispatcher::handle_session_control(int fd, const UdsRequest& req) {
    if (req.params.empty()) {
        return {true, kNrcIncorrectMessageLength, {}};
    }
    const std::uint8_t sub = req.params[0];
    if (sub != 0x01 && sub != 0x02 && sub != 0x03) {
        return {true, kNrcSubFunctionNotSupported, {}};
    }
    sessions_.set(fd, static_cast<Session>(sub));
    LOG_INFO("fd=%d session -> 0x%02x", fd, sub);
    return {false, 0, {sub}};
}

UdsResponse UdsDispatcher::handle_read_data(int fd, const UdsRequest& req) {
    (void)fd;
    if (req.params.size() < 2) {
        return {true, kNrcIncorrectMessageLength, {}};
    }
    const std::uint16_t did = static_cast<std::uint16_t>((req.params[0] << 8) | req.params[1]);
    if (did == 0xF190) {
        std::vector<std::uint8_t> data = {req.params[0], req.params[1]};  // DID hi/lo
        data.insert(data.end(), version_.begin(), version_.end());
        return {false, 0, std::move(data)};
    }
    return {true, kNrcRequestOutOfRange, {}};
}

UdsResponse UdsDispatcher::handle_write_data(int fd, const UdsRequest& req) {
    if (req.params.size() < 2) {
        return {true, kNrcIncorrectMessageLength, {}};
    }
    // 会话状态机：0x2E 仅允许在编程会话(0x02)下执行
    if (sessions_.get(fd) != Session::kProgramming) {
        return {true, kNrcServiceNotSupportedInSession, {}};
    }
    return {false, 0, {req.params[0], req.params[1]}};  // [0x6E][DID]
}

UdsResponse UdsDispatcher::handle_routine_control(int fd, const UdsRequest& req) {
    (void)fd;
    if (req.params.size() < 3) {
        return {true, kNrcIncorrectMessageLength, {}};
    }
    return {false, 0, {req.params[0], req.params[1], req.params[2]}};  // [0x71][subfunc][routineId]
}

UdsResponse UdsDispatcher::handle_request_download(int fd, const UdsRequest& req) {
    if (sessions_.get(fd) != Session::kProgramming) {
        return {true, kNrcServiceNotSupportedInSession, {}};
    }
    // 0x34：DataFormatIdentifier(1) + AddressAndLengthFormatIdentifier(1) + memAddr + memSize
    if (req.params.size() < 2) {
        return {true, kNrcIncorrectMessageLength, {}};
    }
    const std::uint8_t length_format = req.params[1];
    const int addr_len = length_format & 0x0F;
    const int size_len = (length_format >> 4) & 0x0F;
    if (addr_len < 1 || addr_len > 4 || size_len < 1 || size_len > 4) {
        return {true, kNrcRequestOutOfRange, {}};
    }
    if (req.params.size() < static_cast<std::size_t>(2 + addr_len + size_len)) {
        return {true, kNrcIncorrectMessageLength, {}};
    }

    std::uint32_t size = 0;
    const std::uint8_t* size_ptr = req.params.data() + 2 + addr_len;
    for (int i = 0; i < size_len; ++i) {
        size = (size << 8) | size_ptr[i];
    }

    if (!download_.Start(fd, size)) {
        return {true, kNrcConditionsNotCorrect, {}};
    }
    // [0x74][LengthFormatIdentifier=0x20][maxNumberOfBlockLength=4096]
    return {false, 0, {0x20, 0x10, 0x00}};
}

UdsResponse UdsDispatcher::handle_transfer_data(int fd, const UdsRequest& req) {
    if (sessions_.get(fd) != Session::kProgramming) {
        return {true, kNrcServiceNotSupportedInSession, {}};
    }
    if (req.params.size() < 2) {
        return {true, kNrcIncorrectMessageLength, {}};
    }
    const std::uint8_t counter = req.params[0];
    if (!download_.WriteBlock(fd, req.params.data() + 1, req.params.size() - 1, counter)) {
        return {true, kNrcWrongBlockSequenceCounter, {}};
    }
    return {false, 0, {counter}};  // [0x76][counter]
}

UdsResponse UdsDispatcher::handle_request_transfer_exit(int fd, const UdsRequest& req) {
    (void)req;
    if (sessions_.get(fd) != Session::kProgramming) {
        return {true, kNrcServiceNotSupportedInSession, {}};
    }
    if (!download_.Finish(fd)) {
        return {true, kNrcUploadDownloadNotAccepted, {}};
    }
    return {false, 0, {}};  // [0x77]
}

}  // namespace uds
