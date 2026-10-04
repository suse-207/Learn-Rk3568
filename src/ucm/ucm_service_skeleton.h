#pragma once

#include "ota/ucm_service.h"

namespace com {
namespace skeleton {
class BindSkeleton;
}  // namespace skeleton
}  // namespace com

namespace ucm {

/// Exposes an ota::UcmService as a SOME-IP (com) service: registers the
/// TransferStart / TransferData / TransferExit / ProcessSwPackage method
/// handlers on a BindSkeleton.
class UcmServiceSkeleton {
public:
    explicit UcmServiceSkeleton(ota::UcmService& service) : service_(service) {}

    void Register(com::skeleton::BindSkeleton& bind);

private:
    ota::UcmService& service_;
};

}  // namespace ucm
