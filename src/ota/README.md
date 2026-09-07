# ota

M6/M7/M8 实现 OTA Manager：

- 状态机：IDLE -> CHECKING -> DOWNLOADING -> VERIFYING -> INSTALLING -> READY_TO_SWITCH -> SWITCHING -> REBOOTING -> BOOT_VERIFY -> SUCCESS
- Download / Verify / Slot / Boot Control
- 故障注入与 rollback

