# Drive fence design

> Status: proposed design, not implemented.
> Last reviewed: 2026-11-14.
> Scope: the Linux/KDE Plasma drive fence in the current Qt/C++ and QML architecture.

## 1. Product direction

The drive fence should be a compact, configurable overview of physical storage devices and their volumes. It must remain useful with a single system SSD and with many disks, partitions, USB devices, and network mounts.

The user-approved default direction is:

- Large cards.
- USB devices in a separate group.
- Detailed information.
- A physical device is the primary card; its partitions are nested inside it.
- The user can hide individual devices and partitions, choose which fields are visible, and tune the layout.
- The fence should not consume half the desktop by default. The initial size target is approximately 32% of screen width and 60% of screen height, with the fence's normal resize behavior remaining available.
- One or two columns are selected automatically for the default large-card layout. The user may choose a fixed column count.
- The content area scrolls internally; the fence header and summary remain visible.

This document is a design contract for subsequent implementation work. It does not claim that any of these enhancements are already present.

## 2. Current implementation

The current implementation is a flat list:

- `package/contents/ui/DrivesView.qml` renders `Core.drives` with a `ListView`.
- `core/drives.h` defines a flat `Alveus::Drive` record for a mounted volume or an unmounted mountable device.
- `core/drives.cpp` combines mounted-volume information from `QStorageInfo` with unmounted candidates parsed from `lsblk -J`.
- The current `lsblk` request asks for `NAME,PATH,LABEL,FSTYPE,MOUNTPOINTS,SIZE,RM,HOTPLUG,TYPE`; it does not request enough metadata to describe physical devices, transport, model, vendor, partition type, or stable partition identity.
- The current unmounted-volume helper filters out partitions smaller than 2 GiB and only returns mountable filesystem types. This is a discovery shortcut, not a user-configurable visibility policy.
- Drive-related actions currently include open, mount/unmount, open terminal, properties, refresh, and drag-and-drop to a mounted writable volume.
- The global settings dialog does not yet contain drive-specific settings. Generic fence geometry and appearance are already persisted.

The redesign must preserve existing working actions and avoid turning a display redesign into a rewrite of the desktop organizer.

## 3. Information architecture

### 3.1 Top-level groups

The drive fence can display these groups independently:

1. Internal devices.
2. USB/removable devices.
3. Other external devices (for example, Thunderbolt or removable media not classified as USB).
4. Network storage.
5. Other/virtual volumes, when explicitly enabled.

Each group has an independent visibility switch, sort order, and collapsed state. Empty groups are hidden by default. USB is a separate group by default, as requested.

A device must appear in only one group. Classification should prefer explicit transport metadata (for example `TRAN=usb`) and supplement it with removable/hotplug metadata and the Linux device/sysfs topology. The `RM` flag alone must not be treated as proof that a device is a USB device.

### 3.2 Physical device and partition hierarchy

A physical block device is the parent card. Its child partitions/volumes appear inside the card and can be expanded or collapsed independently.

Example:

- Samsung SSD 2 TB
  - EFI System
  - /
  - /home
  - /data
  - Recovery

The card may show a summary of the physical device, but used/free capacity must be calculated from actual mounted filesystems. Do not invent a single "used" value by adding partition sizes or counting overlapping mount points.

Devices without partitions but with a filesystem may be represented as a device card with one volume entry. Network shares are volume cards in the Network storage group, not fake physical disks. LVM, dm-crypt, RAID, and other virtual block devices must be represented without duplicating their underlying physical devices as if they were separate user-visible volumes. The exact grouping rules must be covered by fixtures before implementation.

Partition rows should be collapsed by default to keep the desktop compact. The expanded/collapsed state is remembered per device within that fence.

### 3.3 Card layouts

Supported view modes:

- **Large cards** — default; icon, name, state, capacity bar, used/free values, and selected technical details.
- **Medium cards** — reduced icon and less secondary information.
- **Compact list** — one-line rows with a name, capacity bar, and selected value.

Column options:

- Automatic (default): one column when the fence is narrow; two when the available width is sufficient.
- Fixed 1, 2, or 3 columns.
- Cards wrap within the fence width; they must not force the fence wider than its configured geometry.

The content list scrolls inside the fence. The header, refresh/settings actions, and optional storage summary remain stable while scrolling.

### 3.4 Storage summary

Optional summary at the top:

- Total capacity.
- Used capacity.
- Available capacity.
- Optional used percentage.

Only include values from visible, eligible volumes. Avoid double-counting a physical device and its child partitions, duplicate mount points, snapshots, or stacked block devices. If an accurate aggregate cannot be computed, hide the aggregate or label its scope clearly rather than showing a misleading total.

## 4. Visibility and filtering

Visibility is configurable at three levels:

1. Global defaults for all drive fences.
2. Per-fence overrides.
3. Per-device or per-partition exceptions.

### 4.1 Device and volume switches

The user can independently show/hide:

- Internal devices.
- USB devices.
- Other removable/external devices.
- Network volumes.
- Unmounted mountable partitions.
- Optical media.
- Virtual/loop devices.
- Physical-device cards.
- Nested partition rows.
- Storage summary.

The user can choose whether the partition list contains all eligible partitions, mounted partitions only, or only manually selected partitions.

### 4.2 Automatic service-partition filters

Provide separate rules for known technical partitions:

- EFI System Partition.
- Microsoft Reserved.
- Windows Recovery.
- Linux swap.
- BIOS boot.
- Other known service/diagnostic partitions.
- Pseudo filesystems and transient mounts.

Use reliable metadata where available, including partition type GUID, partition label, filesystem type, device type, and mount point. A name-only heuristic must not silently hide a data volume.

Do not automatically hide a partition only because it is smaller than 2 GiB. A configurable size threshold may be offered as an opt-in advanced filter, but it is disabled by default. This replaces the current hard-coded size cutoff with an explicit user choice.

Default behavior: hide known service partitions (EFI, recovery, swap, BIOS boot) from the ordinary view, but allow them to be revealed through settings or the temporary "Show hidden" action.

### 4.3 Manual hide/show and exception precedence

Every device and partition has context actions:

- Hide this device/partition.
- Always show this device/partition.
- Show properties.
- Show its partitions / hide its partitions.
- Pin to top / remove pin.
- Restore default visibility.

A dedicated Hidden items page lists manually hidden entries and allows restoring them.

Visibility precedence, from strongest to weakest:

1. Explicit per-item **Always show** exception.
2. Explicit per-item **Hide** exception.
3. Automatic service-partition and size rules.
4. Group-level and global visibility switches.

The implementation must define the exact interaction between an item exception and a group switch. An explicit item exception may override automatic filters, but it must not force a disabled top-level group to appear unless the user explicitly chooses that behavior in the UI.

"Show hidden temporarily" is a view-only override. It must not alter saved rules or mount/unmount any device.

### 4.4 Stable identity

Manual visibility, pinning, and expansion state must not be keyed only by a transient display name.

Prefer stable metadata when available:

- Physical device: WWN or another stable hardware identifier; fall back to canonical device path/sysfs identity.
- Partition: PARTUUID; fall back to filesystem UUID plus device identity, then canonical device path.

Handle duplicate or cloned UUIDs by disambiguating with device identity. Device paths can change after reconnect, so path-only persistence is a fallback, not the preferred key.

Serial numbers are optional display metadata, disabled by default, and must never be written to routine diagnostics. Do not use a serial number as the only identity key.

## 5. Settings model

### 5.1 Global defaults and per-fence overrides

Provide a **Drives** page in global Alveus settings for defaults. Provide drive-specific controls in the settings of a fence whose portal is `drives:`.

Per-fence settings override global defaults only where an override is explicitly set. Unset values inherit the current global default. Include actions to:

- Restore this option to the global default.
- Reset all drive options for this fence.
- Reset global drive defaults.

Do not duplicate the existing generic fence geometry state. Width and height controls should update the fence's existing geometry, and manual drag/resize must remain authoritative.

### 5.2 Appearance and layout

- Card style: large / medium / compact.
- Column count: automatic / 1 / 2 / 3.
- Card density and icon size.
- Panel width and height controls, with numeric values.
- Internal scrolling on/off (on by default when content exceeds the available area).
- Summary panel on/off.
- Usage bar on/off.
- Rounded corners, spacing, and accent color where consistent with the existing Alveus theme controls.

Initial size target for a newly created drive fence: about 32% of screen width and 60% of screen height. This is a starting geometry, not a hard limit. Preserve existing user geometry and do not resize existing fences during upgrade.

### 5.3 Grouping and sorting

- Show/hide each top-level group.
- Keep USB in its own group (default on).
- Collapse empty groups (default on).
- Group order: user-configurable.
- Sort by manual order, name, type, total capacity, free capacity, used percentage, or connection state.
- Pin selected devices to the top.
- Remember each group's collapsed state.

### 5.4 Information density and fields

Presets:

- **Compact:** name, status, total capacity, usage bar.
- **Standard:** compact fields plus used/free values and filesystem.
- **Detailed:** default; standard fields plus mount point, device type/interface, read-only state, and available model/vendor information.
- **Custom:** independent field checkboxes.

Potential fields:

- Friendly name/label.
- Device path.
- Device type (physical, partition, logical, network).
- Bus/transport (NVMe, SATA, USB, etc.).
- Model and vendor.
- Total capacity.
- Used and free capacity.
- Usage percentage and bar.
- Filesystem.
- Mount point(s).
- Mounted/unmounted state.
- Read-only state.
- Removable/hotplug state.
- Partition label and partition type.
- Serial number (off by default).

If a field is unavailable on the current device or platform, omit it or show an explicit unavailable state; never fabricate a value.

### 5.5 Refresh behavior

Keep the existing 15-second refresh as the initial default. Offer automatic refresh intervals (for example, 5, 15, 30, or 60 seconds) and manual refresh. Do not run a separate `lsblk` process for each delegate. A single discovery pass should update the model.

## 6. Interaction behavior

Preserve the existing single-click preference and double-click behavior:

- Open a mounted volume in the file manager.
- Mount an unmounted eligible volume, then open it only after mount success.
- Respect read-only state for drag-and-drop.
- Keep existing drag-and-drop copy/move behavior.
- Keep open-terminal and properties actions where applicable.

Context menu actions should be appropriate to the selected object:

- Open / Mount and open.
- Mount / Unmount.
- Safely remove device (only when the entire removable device can be safely unmounted and the system reports that power-off/eject is supported).
- Open terminal.
- Properties.
- Hide / Always show.
- Pin / Unpin.
- Expand / Collapse partitions.
- Refresh.

Do not offer unmount for the system root, and do not offer unsafe eject for an individual partition. Safe removal must be explicit and must report failures. Never auto-mount, auto-unmount, format, repartition, or power off a device as a side effect of refreshing or hiding it.

Low-space indicators should have configurable thresholds. Discovery/mount errors should use the existing message and diagnostic paths with actionable, rate-limited details.

## 7. Data discovery and architecture

### 7.1 Source responsibilities

- Use `lsblk` as the source of block-device topology and parent/child relationships.
- Request optional metadata such as `PKNAME`, `TRAN`, `MODEL`, `VENDOR`, `WWN`, `SERIAL`, `UUID`, `PARTUUID`, `PARTTYPE`, `PARTLABEL`, `RO`, `RM`, `HOTPLUG`, `TYPE`, `SIZE`, `FSTYPE`, `LABEL`, `PATH`, and `MOUNTPOINTS` where supported by the installed util-linux version.
- Use `QStorageInfo` to enrich mounted filesystems with live total/free/available capacity and read-only state.
- Merge records by canonical device path and stable identifiers, avoiding duplicate mounted/unmounted entries.
- Treat multiple mount points as a list; do not discard additional mount points merely because one device was seen already.

Some `lsblk` columns vary across util-linux versions. Probe supported columns or retry with a known baseline column set if an optional column is unavailable. Failure to obtain optional metadata must not make the whole drive list disappear.

### 7.2 Model shape

The implementation should expose a structured hierarchy to QML, conceptually:

- Device: stable ID, device path, friendly name, model/vendor, transport, device type, removable/hotplug flags, total size, and child volumes.
- Volume/partition: stable ID, device path, label, filesystem, partition type, mount point list, mounted/read-only state, total/free/used values where known, and parent device ID.
- Network volume: separate volume entry with network source and mount point, without a fake physical parent.

The exact C++ type names and QML API can be selected during implementation, but the public QML model must preserve the hierarchy and optional fields.

### 7.3 Performance and failure behavior

- Keep `lsblk` asynchronous and bounded by the existing helper timeout.
- Keep the last valid model if discovery fails or returns malformed JSON.
- Do not block the QML/Plasma UI thread while parsing or refreshing devices.
- Avoid unnecessary full-model resets when only one device or usage value changes.
- Coalesce repeated device-change events and avoid high-frequency diagnostic spam.

## 8. Persistence and compatibility

- Global drive defaults belong in the existing settings object under a namespaced drive configuration.
- Per-fence overrides belong in an optional drive-options object on the drives fence.
- Per-device exceptions and pinned/expanded state are scoped to the relevant fence unless the user explicitly chooses a global rule.
- Missing fields in existing saved state must resolve to defaults. Existing layouts must load unchanged.
- Validate new persisted values and reject malformed option objects safely.
- Avoid a store-version bump unless the final schema requires one; if a bump is needed, add a migration and regression tests.
- Unknown future fields should not be silently destroyed during unrelated updates.

## 9. Localization, accessibility, and privacy

- Route all user-facing strings through `i18n()`.
- Update the English source strings and both Ukrainian and Russian translation catalogs.
- Controls need accessible labels, keyboard navigation, visible focus, and tooltips for technical fields.
- Respect Alveus privacy mode; do not expose drive labels or paths when the fence is hidden for screen sharing.
- Keep serial numbers hidden by default and out of diagnostics.
- No telemetry or automatic log upload.

## 10. Test plan

### Core/parser tests

Use synthetic `lsblk` JSON fixtures and temporary data only. Cover:

- One disk with five or more partitions.
- Multiple physical disks with mixed SATA, NVMe, and USB transport.
- A USB disk with several partitions.
- Mounted and unmounted partitions in the same device tree.
- Multiple mount points on one filesystem/device.
- A disk with no partitions.
- Network shares.
- LVM, dm-crypt, RAID, and other stacked devices.
- Missing optional `lsblk` columns and older output formats.
- Malformed JSON, timeout, and retry while preserving the last good model.
- Duplicate UUIDs and stable-ID fallbacks.
- No double counting in summary statistics.

### Settings/filter tests

- Global defaults apply when no per-fence override exists.
- Per-fence values override only their corresponding global values.
- Reset-one-option and reset-all restore inheritance correctly.
- Hidden and always-show exceptions follow the documented precedence.
- EFI/recovery/swap filters are configurable.
- Size threshold is off by default.
- Hidden state survives save/reload and device reconnect where stable IDs are available.
- Old saved layouts load without migration loss.

### UI tests

- Large, medium, and compact layouts render.
- Auto and fixed column counts work at narrow and wide fence widths.
- Internal scrolling does not move the fence header.
- USB is grouped separately and empty groups collapse.
- A device with five partitions expands/collapses without changing the physical-device count.
- Hidden items can be restored.
- Existing open, mount/unmount, terminal, properties, and drag-and-drop behavior remains intact.
- Critical QML warnings fail the offscreen UI harness.

CI/offscreen tests do not replace testing with real SATA, NVMe, USB, network, LVM, and encrypted devices in a live KDE Plasma session.

## 11. Implementation stages

1. **Core discovery model:** introduce physical-device/volume hierarchy, optional metadata, stable IDs, robust `lsblk` parsing, and fixture tests. Preserve the existing flat QML-facing API temporarily if needed during transition.
2. **Settings and visibility engine:** global defaults, per-fence overrides, group switches, automatic filters, manual exceptions, hidden-items list, and persistence tests.
3. **Drive fence UI:** large/medium/compact layouts, adaptive columns, nested partitions, USB group, summary, internal scrolling, and detailed fields.
4. **Interactions and polish:** context actions, pin/sort, low-space thresholds, safe-removal capability checks, accessibility, and translations.
5. **Verification:** full CTest/QML lint/translation CI, RPM build, and live Plasma tests with multiple real storage configurations.

Each implementation stage should be a focused branch and PR. Do not merge until the relevant CI checks pass. Do not install or activate the widget on the user's desktop without explicit approval.

## 12. Acceptance criteria

The redesign is ready when:

- Physical disks and their partitions are shown as a hierarchy without duplicates.
- USB devices are independently grouped.
- Large cards and detailed information are the default, with medium and compact alternatives.
- Users can hide/show any device or partition and configure automatic filters.
- Global defaults and per-fence overrides persist safely.
- Layout adapts to the fence geometry and keeps scrolling inside the content area.
- Existing drive operations continue to work and destructive actions are never implicit.
- Core, UI, translation, and packaging checks pass; live Plasma limitations are documented.
