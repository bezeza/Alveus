%global plugin_id ua.bezeza.alveus

Name:           alveus
Version:        0.03.1
Release:        0.1.beta%{?dist}
Summary:        Desktop fences for KDE Plasma 6

# Most code GPL-2.0-or-later; two widget-handle QML files copied from
# KDE plasma-desktop are LGPL-2.0-or-later
License:        GPL-2.0-or-later AND LGPL-2.0-or-later
URL:            https://github.com/bezeza/alveus-plasma
Source0:        %{url}/archive/v%{version}/alveus-plasma-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  qt6-rpm-macros
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6DBus)

Requires:       plasma-workspace >= 6.0
Requires:       coreutils
Requires:       kf6-kirigami
Requires:       qt6-qtdeclarative%{?_isa}
# thumbnails for videos and PDFs
Recommends:     ffmpegthumbnailer
Recommends:     poppler-utils
# "Send to phone" and the "From phone" portal
Suggests:       kde-connect

%description
Alveus turns the KDE Plasma desktop into fences, like Stardock Fences:
drag a box on the desktop to make a fence, and new files sort themselves
into fences by type, name, download site or date. Files never move: they
stay in the Desktop folder, fences only group them.

Also: roll-up fences, tabs, folder portals, desktop pages, per-activity
fences, frosted glass, honeycomb view, saved layouts, undo, a tidy-up
helper, privacy mode while sharing the screen, KRunner search, and
sending files to a phone with KDE Connect.

%prep
%autosetup -n alveus-plasma-%{version}

%build
%cmake -DALVEUS_QML_DIR=%{_qt6_qmldir}
%cmake_build

%install
%cmake_install
%find_lang plasma_applet_%{plugin_id}

%files -f plasma_applet_%{plugin_id}.lang
%license LICENSE LICENSES/*
%doc README.md
%{_qt6_qmldir}/ua/bezeza/alveus/
%{_datadir}/plasma/plasmoids/%{plugin_id}/
%{_datadir}/krunner/dbusplugins/%{plugin_id}.runner.desktop

%changelog
* Sun Nov 01 2026 bezeza - 0.03.1-0.1.beta
- Preserve symlinks or fail safely on filesystems without symlink support
- Reject FIFOs, sockets and device nodes before copy or move
- Validate layout, placement and snapshot schemas before loading
- Keep trash metadata when a restore leaves the source copy in Trash
- Add regression tests for damaged settings, symlinks and special files
* Sun Nov 01 2026 bezeza - 0.03-0.1.beta
- Copy and move run in the background with progress and Cancel; the desktop never freezes
- Regular-file fallback works on FAT/vfat; unsupported symlinks fail safely without deleting the source
- Growing files no longer cause a desktop rescan on every write
- Messages translated; "settings cannot be saved" shown once
* Sun Nov 01 2026 bezeza - 0.02-0.1.beta
- Stability, persistence, file operation, thumbnail and undo fixes
* Sun Nov 01 2026 bezeza - 0.01-0.1.beta
- First beta
