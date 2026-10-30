Name:       harbour-onloc

Summary:    OnLoc
Version:    1.1.0
Release:    1
License:    MIT
URL:        https://github.com/onloc-app
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Positioning)
# No pkgconfig(Qt5LinguistTools) virtual exists on Sailfish OS - CMake's
# find_package(Qt5 COMPONENTS LinguistTools) finds it via this package.
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils
BuildRequires:  cmake

%description
Unofficial native client for OnLoc, the self-hosted device tracker. See
your devices and their last known positions and location history, ring,
lock or flash them, and optionally report this phone's own position to your
server. Talks only to the OnLoc server you sign in to.

%prep
%setup -q -n %{name}-%{version}

%build

%cmake

%make_build


%install
%make_install


desktop-file-install --delete-original         --dir %{buildroot}%{_datadir}/applications                %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
